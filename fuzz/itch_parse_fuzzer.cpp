// libFuzzer entry over the full parse path: frame arbitrary bytes, decode
// every frame, touch every field. Contract: any input either parses to the
// end or throws FramingError. Anything else (crash, sanitizer report,
// other exception) is a finding and must become a regression test.
#include "itch/decoder.hpp"
#include "itch/messages.hpp"
#include "itch/reader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace ob::itch;

struct FieldTouchingHandler {
    uint64_t field_sum = 0; // keeps every field read observable

    void operator()(const SystemEvent& m) {
        field_sum += m.header.timestamp.value() + static_cast<unsigned char>(m.event_code);
    }
    void operator()(const StockDirectory& m) {
        field_sum +=
            m.round_lot_size.value() + m.etp_leverage_factor.value() + m.stock.trimmed().size();
    }
    void operator()(const AddOrder& m) {
        field_sum += m.order_ref.value() + m.shares.value() + m.price.value() +
                     m.stock.trimmed().size() + static_cast<unsigned char>(m.side);
    }
    void operator()(const AddOrderMpid& m) {
        (*this)(m.add);
        field_sum += m.mpid.trimmed().size();
    }
    void operator()(const OrderExecuted& m) {
        field_sum += m.order_ref.value() + m.executed_shares.value() + m.match_number.value();
    }
    void operator()(const OrderExecutedPrice& m) {
        (*this)(m.executed);
        field_sum += m.execution_price.value() + static_cast<unsigned char>(m.printable);
    }
    void operator()(const OrderCancel& m) {
        field_sum += m.order_ref.value() + m.canceled_shares.value();
    }
    void operator()(const OrderDelete& m) { field_sum += m.order_ref.value(); }
    void operator()(const OrderReplace& m) {
        field_sum +=
            m.original_ref.value() + m.new_ref.value() + m.shares.value() + m.price.value();
    }
    void operator()(const Trade& m) {
        field_sum +=
            m.order_ref.value() + m.shares.value() + m.price.value() + m.match_number.value();
    }
    void on_skipped(const Frame& frame) { field_sum += frame.body.size(); }
    void on_unknown(const Frame& frame) { field_sum += frame.body.size(); }
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const std::span<const std::byte> stream{reinterpret_cast<const std::byte*>(data), size};
    FrameReader reader(stream);
    FieldTouchingHandler handler;
    try {
        while (const auto frame = reader.next()) {
            decode(*frame, handler);
        }
    } catch (const FramingError&) {
        // rejected input, the expected failure mode
    }
    return 0;
}
