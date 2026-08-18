// Fuzz entry point for the ITCH framing + decode path.
//
// The target is the whole read path a captured stream takes: FrameReader over
// arbitrary bytes, then decode() on every frame it yields. FramingError is the
// expected outcome for most inputs and is caught; anything else escaping this
// function is a bug. The sink touches every field of every decoded message so
// the sanitizers see real loads rather than a cast the optimizer can drop.

#include "itch/decoder.hpp"
#include "itch/messages.hpp"
#include "itch/reader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace {

using namespace ob::itch;

struct Sink {
    uint64_t acc = 0;

    void byte(char c) { acc += static_cast<unsigned char>(c); }

    void take(const Header& h) {
        byte(h.type);
        acc += h.locate.value();
        acc += h.tracking.value();
        acc += h.timestamp.value();
    }

    template <std::size_t N> void take(const Alpha<N>& a) {
        acc += a.trimmed().size();
        for (const char c : a.padded()) {
            byte(c);
        }
    }

    void operator()(const SystemEvent& m) {
        take(m.header);
        byte(m.event_code);
    }

    void operator()(const StockDirectory& m) {
        take(m.header);
        take(m.stock);
        byte(m.market_category);
        byte(m.financial_status);
        acc += m.round_lot_size.value();
        byte(m.round_lots_only);
        byte(m.issue_classification);
        take(m.issue_subtype);
        byte(m.authenticity);
        byte(m.short_sale_threshold);
        byte(m.ipo_flag);
        byte(m.luld_reference_tier);
        byte(m.etp_flag);
        acc += m.etp_leverage_factor.value();
        byte(m.inverse_indicator);
    }

    void operator()(const AddOrder& m) {
        take(m.header);
        acc += m.order_ref.value();
        byte(m.side);
        acc += m.shares.value();
        take(m.stock);
        acc += m.price.value();
    }

    void operator()(const AddOrderMpid& m) {
        (*this)(m.add);
        take(m.mpid);
    }

    void operator()(const OrderExecuted& m) {
        take(m.header);
        acc += m.order_ref.value();
        acc += m.executed_shares.value();
        acc += m.match_number.value();
    }

    void operator()(const OrderExecutedPrice& m) {
        (*this)(m.executed);
        byte(m.printable);
        acc += m.execution_price.value();
    }

    void operator()(const OrderCancel& m) {
        take(m.header);
        acc += m.order_ref.value();
        acc += m.canceled_shares.value();
    }

    void operator()(const OrderDelete& m) {
        take(m.header);
        acc += m.order_ref.value();
    }

    void operator()(const OrderReplace& m) {
        take(m.header);
        acc += m.original_ref.value();
        acc += m.new_ref.value();
        acc += m.shares.value();
        acc += m.price.value();
    }

    void operator()(const Trade& m) {
        take(m.header);
        acc += m.order_ref.value();
        byte(m.side);
        acc += m.shares.value();
        take(m.stock);
        acc += m.price.value();
        acc += m.match_number.value();
    }

    // Both classes carry a well-framed header, so read it the same way a
    // counting caller would.
    void on_skipped(const Frame& f) { take(message_cast<Header>(f.body)); }
    void on_unknown(const Frame& f) { take(message_cast<Header>(f.body)); }
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    const std::span<const std::byte> stream = std::as_bytes(std::span(data, size));
    Sink sink;
    FrameReader reader(stream);
    try {
        while (const auto frame = reader.next()) {
            decode(*frame, sink);
        }
    } catch (const FramingError& e) {
        // Malformed framing is the expected outcome for most inputs; the reader
        // is required to report it rather than read past the end. Fold the
        // message in so the error path is observable work too.
        sink.acc += std::string_view(e.what()).size();
    }
    // A volatile store the compiler may not elide: without it nothing above is
    // observable and the whole decode can be optimized away.
    const volatile uint64_t observed = sink.acc;
    (void)observed;
    return 0;
}
