#include <cstring>
#include <stdexcept>

#include <catimpl.h>

static bool type_check(IObject* obj, const std::type_info& dst_type) noexcept {
    return runtime_dynamic_cast(obj, typeid(IObject), dst_type);
}

template<typename... Args> [[noreturn]] void throw_invalid_argument(fmt::format_string<Args...> fmt, Args&&... args) {
    throw std::invalid_argument(format_c(std::move(fmt), std::forward<Args>(args)...));
}

static void check_args(const ArgSpec* specs, size_t arg_count, const ITable* args) {
    for (size_t ref = 0; ref < arg_count; ++ref) {
        const char* key;
        auto val = args->get(ref, &key);
        auto&& spec = specs[ref];
        if (!key)
            throw_invalid_argument("missing argument at {}", ref);
        if (std::strcmp(spec.name, key) != 0)
            throw_invalid_argument("invalid argument name at {} (expected '{}', got '{}')", ref, spec.name, key);
        if (spec.required && !val)
            throw_invalid_argument("missing required argument '{}'", spec.name);
        if (auto obj = const_cast<IObject*>(val); obj && spec.type) {
            if (*spec.type == typeid(int64_t) || *spec.type == typeid(double)) {
                if (auto arr = dynamic_cast<INumeric*>(obj); arr) {
                    if (!spec.array && arr->bytes_count() != 8)
                        goto invalid_type;
                    if (arr->sample_type == SampleType::Integer && *spec.type == typeid(double) ||
                        arr->sample_type == SampleType::Float && *spec.type == typeid(int64_t))
                        goto invalid_type;
                } else
                    goto invalid_type;
            } else if (spec.array) {
                if (auto arr = dynamic_cast<ITable*>(obj); arr) {
                    for (size_t i = 0;; ++i) {
                        auto elem = const_cast<IObject*>(arr->get(i, nullptr));
                        if (!elem)
                            break;
                        if (!type_check(elem, *spec.type))
                            goto invalid_type;
                    }
                } else
                    goto invalid_type;
            } else if (!type_check(obj, *spec.type))
                goto invalid_type;
        }
        continue;
    invalid_type:
        throw_invalid_argument("invalid type for argument '{}' (expected '{}{}' or derived)", spec.name,
                               spec.type->name(), spec.array ? "[]" : "");
    }
}

static void check_args(IFunction* func, const ITable* args) {
    size_t len = 0;
    auto specs = func->get_arg_specs(&len);
    check_args(specs, len, args);
}

struct StepDesc {
    std::string enzyme_id;
    std::string func_name;
    cat_ptr<ITable> args;
};

class StepDescLess {
    static int cmp_vi(const ISubstrate* l, const ISubstrate* r) noexcept {
        auto lvi = l->get_video_info();
        auto rvi = r->get_video_info();
        return std::memcmp(&lvi, &rvi, sizeof(VideoInfo));
    }

    static int cmp_bytes(const IBytes* l, const IBytes* r) noexcept {
        auto lsize = l->size();
        auto rsize = r->size();
        if (auto cmp = lsize <=> rsize; cmp != 0)
            return cmp < 0 ? -1 : 1;
        return std::memcmp(l->data(), r->data(), lsize);
    }

    static int cmp_table(const ITable* l, const ITable* r) noexcept {
        for (size_t ref = 0;; ++ref) {
            auto lval = l->get(ref, nullptr);
            auto rval = r->get(ref, nullptr);
            if (auto lnul = lval == nullptr, rnul = rval == nullptr; lnul && rnul)
                return 0;
            else if (lnul || rnul)
                return lnul ? -1 : 1;
            if (auto ltab = dynamic_cast<const ITable*>(lval), rtab = dynamic_cast<const ITable*>(rval); ltab || rtab) {
                if (ltab == nullptr || rtab == nullptr)
                    return ltab ? 1 : -1;
                if (auto cmp = cmp_table(ltab, rtab); cmp != 0)
                    return cmp;
            } else if (auto ldat = dynamic_cast<const IBytes*>(lval), rdat = dynamic_cast<const IBytes*>(rval);
                       ldat || rdat) {
                if (ldat == nullptr || rdat == nullptr)
                    return ldat ? 1 : -1;
                if (auto cmp = cmp_bytes(ldat, rdat); cmp != 0)
                    return cmp;
            } else if (auto lsub = dynamic_cast<const ISubstrate*>(lval), rsub = dynamic_cast<const ISubstrate*>(rval);
                       lsub || rsub) {
                if (lsub == nullptr || rsub == nullptr)
                    return lsub ? 1 : -1;
                if (auto cmp = cmp_vi(lsub, rsub); cmp != 0)
                    return cmp;
            } else if (auto cmp = lval <=> rval; cmp != 0)
                return cmp < 0 ? -1 : 1;
        }
    }

  public:
    bool operator()(const StepDesc& l, const StepDesc& r) const noexcept {
        if (auto cmp = l.enzyme_id <=> r.enzyme_id; cmp != 0)
            return cmp < 0;
        if (auto cmp = l.func_name <=> r.func_name; cmp != 0)
            return cmp < 0;
        return cmp_table(l.args.get(), r.args.get()) < 0;
    }
};

class Pathway final : public Object, virtual public IPathway, public Shuttle {
    std::multimap<const StepDesc, Substrate*, StepDescLess> pool;

    IFunction* get_func(const char* enzyme_id, const char* func_name) noexcept {
        auto enzymes = this->nucl.enzymes.get();
        auto enzyme = dynamic_cast<const IEnzyme*>(enzymes->get(enzymes->find(enzyme_id), nullptr));
        auto funcs = enzyme->get_functions();
        auto func = dynamic_cast<const IFunction*>(funcs->get(funcs->find(func_name), nullptr));
        return const_cast<IFunction*>(func);
    }

    static void update_sources(ITable* dst, const ITable* src) noexcept {
        for (size_t ref = 0;; ++ref) {
            auto dval = dst->get(ref, nullptr);
            auto sval = src->get(ref, nullptr);
            if (dval == nullptr)
                return;
            if (auto dtab = dynamic_cast<const ITable*>(dval), stab = dynamic_cast<const ITable*>(sval); dtab)
                update_sources(const_cast<ITable*>(dtab), stab);
            else if (auto dsub = dynamic_cast<const Substrate*>(dval), ssub = dynamic_cast<const Substrate*>(sval);
                     dsub)
                const_cast<Substrate*>(dsub)->filter = ssub->filter;
        }
    }

    cat_ptr<ITable> create_shim(const ITable* args) const noexcept {
        cat_ptr<ITable> r;
        nucl.create_table(0, r.put());
        for (size_t ref = 0;; ++ref) {
            const char* key;
            auto val = args->get(ref, &key);
            if (val == nullptr)
                return r;
            if (auto tab = dynamic_cast<const ITable*>(val); tab)
                r->set(ref, create_shim(tab).get(), key);
            else if (auto sub = dynamic_cast<const Substrate*>(val); sub)
                r->set(ref, new Substrate{nucl, sub->filter}, key);
            else
                r->set(ref, val, key);
        }
    }

  public:
    void clone(IObject** out) const noexcept final {
        not_implemented();
    }

    void add_step(const char* enzyme_id, const char* func_name, const ITable* args, ISubstrate** out) final {
        auto func = get_func(enzyme_id, func_name);
        check_args(func, args);
        StepDesc desc{
            enzyme_id,
            func_name,
            wrap_cat_ptr(const_cast<ITable*>(args)),
        };
        auto [bg, ed] = pool.equal_range(desc);
        for (auto it = bg; it != ed; ++it)
            if (auto substrate = it->second; substrate->is_unique()) {
                update_sources(it->first.args.get(), args);
                *out = substrate;
                substrate->add_ref();
                return;
            }
        auto shim = create_shim(args);
        cat_ptr<const IObject> obj;
        func->invoke(shim.get(), obj.put_const());
        auto filter = obj.query<const IFilter>();
        auto substrate = nucl.register_filter(filter.get());
        desc.args = std::move(shim);
        pool.emplace(std::move(desc), dynamic_cast<Substrate*>(substrate));
        *out = substrate;
        substrate->add_ref();
    }

    explicit Pathway(Nucleus& nucl) noexcept : Shuttle(nucl) {}

    ~Pathway() final {
        for (auto&& item : pool) {
            cond_check(item.second->is_unique(), "all substrates created by this pathway are not released");
            nucl.unregister_filter(item.second->filter.get());
        }
    }
};

void Nucleus::create_pathway(IPathway** out) noexcept {
    create_instance<Pathway>(out, *this);
}
