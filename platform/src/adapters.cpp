#include "ophion/platform/adapters.hpp"

#include <algorithm>
#include <utility>

namespace ophion::platform {

OffsetProfile::OffsetProfile(ImageFingerprint expected_image, std::vector<OffsetField> fields)
    : expected_image_(expected_image), fields_(std::move(fields)) {}

Error OffsetProfile::validate_shape() const noexcept {
    if (fields_.empty() || fields_.size() > 256) return Error::ProfileInvalid;
    for (std::size_t index = 0; index < fields_.size(); ++index) {
        if (fields_[index].name.empty() || fields_[index].offset >= kMaxReadBytes) return Error::ProfileInvalid;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (fields_[previous].name == fields_[index].name) return Error::ProfileInvalid;
        }
    }
    return Error::Ok;
}

std::optional<ValidatedOffsetProfile> OffsetProfile::validate(const ImageFingerprint& actual) const {
    if (validate_shape() != Error::Ok || actual != expected_image_) return std::nullopt;
    return ValidatedOffsetProfile{*this};
}

std::optional<std::uint32_t> OffsetProfile::field(std::string_view name) const noexcept {
    const auto match = std::find_if(fields_.begin(), fields_.end(), [name](const OffsetField& field) { return field.name == name; });
    return match == fields_.end() ? std::nullopt : std::optional<std::uint32_t>{match->offset};
}

namespace {
Error unavailable_snapshot(GameSnapshot& output) {
    output = {};
    return Error::Unsupported;
}
} // namespace

Error UnrealAdapter::snapshot(GameSnapshot& output) const { (void)reader_; (void)profile_; return unavailable_snapshot(output); }
Error UnityIl2CppAdapter::snapshot(GameSnapshot& output) const { (void)reader_; (void)profile_; return unavailable_snapshot(output); }
Error Source2Adapter::snapshot(GameSnapshot& output) const { (void)reader_; (void)profile_; return unavailable_snapshot(output); }

} // namespace ophion::platform
