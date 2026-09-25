# RB session state follows the official SHFE schedule

The first profile models RB with explicit auction-submission, auction-matching, continuous-trading, break, and closed phases using Asia/Shanghai virtual time. It includes the official night and day windows recorded in `docs/shfe/official-rules.md`; a night session belongs to the following trading day, while breaks and closed phases reject new orders and cancellations.

**Status:** accepted

**Consequences:** Session transitions are driven by `VirtualClock`. At daily close, all live orders are canceled and their freezes released; positions remain until the explicit trading-day rollover event.
