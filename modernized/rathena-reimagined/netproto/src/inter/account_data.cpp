#include "rathena/netproto/inter/account_data.hpp"

#include "fixed_string.hpp"

namespace rathena::netproto::inter {

namespace {
constexpr std::size_t kOffAid = 2;
constexpr std::size_t kOffEmail = 6;
constexpr std::size_t kOffExpiration = 46;
constexpr std::size_t kOffGroupByte = 50;
constexpr std::size_t kOffCharSlots = 51;
constexpr std::size_t kOffBirthdate = 52;
constexpr std::size_t kOffPincode = 63;
constexpr std::size_t kOffPincodeChange = 68;
constexpr std::size_t kOffIsVip = 72;
constexpr std::size_t kOffCharVip = 73;
constexpr std::size_t kOffCharBilling = 74;
constexpr std::size_t kOffGroupWide = 75;  // v1 only
}  // namespace

CodecResult<std::size_t> account_data_size(ProtocolVersion version) noexcept {
  switch (version) {
    case kProtocolLegacy: return kAccountDataSizeV0;
    case kProtocolV1: return kAccountDataSizeV1;
    default: return CodecError::kUnsupportedVersion;
  }
}

CodecResult<PacketSpec> account_data_spec(ProtocolVersion version) noexcept {
  const auto size = account_data_size(version);
  if (!size) return size.error();
  return PacketSpec::fixed(kOpAccountData, static_cast<std::uint16_t>(size.value()));
}

CodecResult<std::vector<std::uint8_t>> encode(const AccountData& p, ProtocolVersion version) {
  const auto size = account_data_size(version);
  if (!size) return size.error();
  // BR-INTER-003: never truncate. A legacy peer cannot carry group_id > 255.
  if (version == kProtocolLegacy && (p.group_id < 0 || p.group_id > 255)) return CodecError::kNotRepresentable;

  std::vector<std::uint8_t> out(size.value(), 0);
  MutableByteView b(out);
  write_u16le(b, 0, kOpAccountData);
  write_u32le(b, kOffAid, p.account_id);
  if (!detail::write_fixed(b, kOffEmail, kEmailLength, p.email)) return CodecError::kFieldTooLong;
  write_u32le(b, kOffExpiration, p.expiration_time);
  const std::int32_t saturated = p.group_id < 0 ? 0 : (p.group_id > 255 ? 255 : p.group_id);
  out[kOffGroupByte] = static_cast<std::uint8_t>(saturated);
  out[kOffCharSlots] = p.char_slots;
  if (!detail::write_fixed(b, kOffBirthdate, kBirthdateLength, p.birthdate)) return CodecError::kFieldTooLong;
  if (!detail::write_fixed(b, kOffPincode, kPincodeLength, p.pincode)) return CodecError::kFieldTooLong;
  write_u32le(b, kOffPincodeChange, p.pincode_change);
  out[kOffIsVip] = p.is_vip ? 1 : 0;
  out[kOffCharVip] = p.char_vip;
  out[kOffCharBilling] = p.char_billing;
  if (version >= kProtocolV1) write_u32le(b, kOffGroupWide, static_cast<std::uint32_t>(p.group_id));
  return out;
}

CodecResult<AccountData> decode_account_data(ByteView b, ProtocolVersion version) {
  const auto size = account_data_size(version);
  if (!size) return size.error();
  if (b.size() < size.value()) return CodecError::kTruncated;
  if (read_u16le(b, 0) != kOpAccountData) return CodecError::kWrongOpcode;

  AccountData p;
  p.account_id = read_u32le(b, kOffAid);
  p.email = detail::read_fixed(b, kOffEmail, kEmailLength);
  p.expiration_time = read_u32le(b, kOffExpiration);
  p.group_id = version >= kProtocolV1 ? static_cast<std::int32_t>(read_u32le(b, kOffGroupWide))
                                      : static_cast<std::int32_t>(b[kOffGroupByte]);
  p.char_slots = b[kOffCharSlots];
  p.birthdate = detail::read_fixed(b, kOffBirthdate, kBirthdateLength);
  p.pincode = detail::read_fixed(b, kOffPincode, kPincodeLength);
  p.pincode_change = read_u32le(b, kOffPincodeChange);
  p.is_vip = b[kOffIsVip] != 0;
  p.char_vip = b[kOffCharVip];
  p.char_billing = b[kOffCharBilling];
  return p;
}

}  // namespace rathena::netproto::inter
