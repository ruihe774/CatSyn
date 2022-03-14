#include <catimpl.h>


Substrate::Substrate(cat_ptr<const IFilter> filter) noexcept : vi(filter->get_video_info()) {
    filters[std::this_thread::get_id()] = filter.usurp_or_clone();
}

VideoInfo Substrate::get_video_info() const noexcept {
    return vi;
}

void Nucleus::register_filter(const IFilter* in, ISubstrate** out) noexcept {
    create_instance<Substrate>(out, wrap_cat_ptr(in));
}
