#include <string_view>

#include <catimpl.h>

#ifdef _WIN32
#ifdef __clang__
extern "C" void* __RTDynamicCast(void* inptr, long VfDelta, void* SrcType, void* TargetType, int isReference);
#endif
static void* runtime_dynamic_cast(const std::type_info& dst_type, IObject* src) {
    // XXX: I don't know this is correct or not
    return __RTDynamicCast(src, 0, (void*)&typeid(IObject), (void*)&dst_type, 0);
}
#endif

static bool type_check(const std::type_info& dst_type, IObject* src, bool allow_anta = false) {
    if (runtime_dynamic_cast(dst_type, src))
        return true;
    if (allow_anta && dst_type == typeid(ISubstrate) && dynamic_cast<IAntagonist*>(src))
        return true;
    return false;
}

static void check_args(const ArgSpec* specs, size_t arg_count, const ITable* args, bool allow_anta = false) {
    thread_local char buf[1024];
    for (size_t ref = 0; ref < arg_count; ++ref) {
        const char* key;
        auto val = args->get(ref, &key);
        auto&& spec = specs[ref];
        if (!key) {
            std::snprintf(buf, sizeof(buf), "missing argument at %llu", ref);
            throw std::invalid_argument(buf);
        }
        if (std::strcmp(spec.name, key) != 0) {
            std::snprintf(buf, sizeof(buf), "invalid argument name at %llu (expected '%s', got '%s')", ref, spec.name,
                          key);
            throw std::invalid_argument(buf);
        }
        if (spec.required && !val) {
            std::snprintf(buf, sizeof(buf), "missing required argument '%s'", spec.name);
            throw std::invalid_argument(buf);
        }
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
                        if (!type_check(*spec.type, elem))
                            goto invalid_type;
                    }
                } else
                    goto invalid_type;
            } else if (!type_check(*spec.type, obj))
                goto invalid_type;
        }
        continue;
    invalid_type:
        std::snprintf(buf, sizeof(buf), "invalid type for argument '%s' (expected '%s%s' or derived)", spec.name,
                      spec.type->name(), spec.array ? "[]" : "");
        throw std::invalid_argument(buf);
    }
}

static void check_args(IFunction* func, const ITable* args, bool allow_anta = false) {
    size_t len = 0;
    auto specs = func->get_arg_specs(&len);
    check_args(specs, len, args, allow_anta);
}

class Antagonist final : public Object, virtual public IAntagonist {
  public:
    const std::string enzyme_id;
    const std::string func_name;
    const cat_ptr<const ITable> args;

    Antagonist(std::string enzyme_id, std::string func_name, cat_ptr<const ITable> args) noexcept
        : enzyme_id(std::move(enzyme_id)), func_name(std::move(func_name)), args(std::move(args)) {}
};

class Pathway final : public Object, virtual public IPathway, public Shuttle {
    std::map<int, cat_ptr<Antagonist>> slots;
    std::map<Antagonist*, cat_ptr<Substrate>> substrates;

    IFunction* get_func(const char* enzyme_id, const char* func_name) noexcept {
        auto enzymes = this->nucl.enzymes.get();
        auto enzyme = dynamic_cast<const IEnzyme*>(enzymes->get(enzymes->find(enzyme_id), nullptr));
        auto funcs = enzyme->get_functions();
        auto func = dynamic_cast<const IFunction*>(funcs->get(funcs->find(func_name), nullptr));
        return const_cast<IFunction*>(func);
    }

    Antagonist* downcast_anta(const IObject* obj) {
        return &dynamic_cast<Antagonist&>(*const_cast<IObject*>(obj));
    }

    ISubstrate* make_substrate(Antagonist* anta) {
        if (auto it = substrates.find(anta); it != substrates.end())
            return it->second.get();
        auto func = get_func(anta->enzyme_id.c_str(), anta->func_name.c_str());
        size_t len = 0;
        auto specs = func->get_arg_specs(&len);
        cat_ptr<ITable> finalized_args;
        nucl.create_table(len, finalized_args.put());
        for (size_t i = 0; i < len; ++i) {
            const char* key;
            auto val = anta->args->get(i, &key);
            if (*specs[i].type == typeid(ISubstrate)) {
                if (specs[i].array) {
                    auto arr = dynamic_cast<const ITable*>(val);
                    cat_ptr<ITable> finalized_arr;
                    auto size = arr->size();
                    nucl.create_table(size, finalized_arr.put());
                    for (size_t j = 0; j < size; ++j)
                        finalized_arr->set(j, make_substrate(downcast_anta(arr->get(j, nullptr))), key);
                    finalized_args->set(i, finalized_arr.get(), key);
                } else
                    finalized_args->set(i, make_substrate(downcast_anta(val)), key);
            } else
                finalized_args->set(i, val, key);
        }
        cat_ptr<const IObject> out;
        func->invoke(finalized_args.get(), out.put_const());
        return substrates.emplace(anta, out.query<const Substrate>().clone()).first->second.get();
    }

  public:
    void add_step(const char* enzyme_id, const char* func_name, const ITable* args, IAntagonist** out) final {
        check_args(get_func(enzyme_id, func_name), args, true);
        create_instance<Antagonist>(out, enzyme_id, func_name, wrap_cat_ptr(args));
    }

    void set_slot(int id, const IAntagonist* anta) noexcept final {
        if (anta)
            slots.insert_or_assign(id, wrap_cat_ptr(&dynamic_cast<Antagonist&>(*const_cast<IAntagonist*>(anta))));
        else
            slots.erase(id);
    }

    IAntagonist* get_slot(int id) const noexcept final {
        if (auto it = slots.find(id); it != slots.end())
            return it->second.get();
        else
            return nullptr;
    }

    ISubstrate* make_substrate(const IAntagonist* anta) final {
        return make_substrate(downcast_anta(anta));
    }

    explicit Pathway(Nucleus& nucl) noexcept : Shuttle(nucl) {}

    explicit Pathway(const Pathway& other) noexcept
        : Shuttle(other.nucl), slots(other.slots), substrates(other.substrates) {}

    void clone(IObject** out) const noexcept {
        create_instance<Pathway>(out, *this);
    }
};

void Nucleus::create_pathway(IPathway** out) noexcept {
    create_instance<Pathway>(out, *this);
}
