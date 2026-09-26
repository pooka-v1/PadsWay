#include <cstdio>
#include <string>
#include <catch2/catch_amalgamated.hpp>

// ---------------------------------------------------------------------------
// E2EProgress — live progress in the console: a header per test case and one line per case (each
// row/chain of assignments.json, or the whole test case when it has no sections), so a long run
// visibly moves and a stuck case is obvious:
//
//   == Perfiles assignments from cases/assignments.json
//     OK   a -> b                                      0.9 s
//     ..   a -> r2                     <- running now (rewritten in place when it ends)
//
// Only listens: the console reporter still prints the failure details and the final summary.
// A failed assertion breaks the in-progress line first, so its details don't glue onto it.
// ---------------------------------------------------------------------------
namespace {

class E2EProgress : public Catch::EventListenerBase {
public:
    using EventListenerBase::EventListenerBase;

    void testCaseStarting(Catch::TestCaseInfo const& info) override {
        std::printf("\n== %s\n", info.name.c_str());
        std::fflush(stdout);
    }

    // Depth 1 = the test case itself (entered once per GENERATE value), depth 2 = DYNAMIC_SECTION.
    void sectionStarting(Catch::SectionInfo const& info) override {
        ++m_depth;
        if (m_depth == 1) m_caseReported = false;
        if (m_depth == 2) showRunning(info.name);
    }

    void sectionEnded(Catch::SectionStats const& stats) override {
        if (m_depth == 2) {
            showResult(stats);
            m_caseReported = true;
        } else if (m_depth == 1 && !m_caseReported) {
            showResult(stats);   // test case without sections: one line for all of it
        }
        --m_depth;
    }

    void assertionEnded(Catch::AssertionStats const& stats) override {
        if (!stats.assertionResult.isOk() && m_lineOpen) {
            std::printf("\n");
            m_lineOpen = false;
        }
    }

private:
    static constexpr int kNameWidth = 44;

    void showRunning(const std::string& name) {
        std::printf("  ..   %-*s", kNameWidth, name.c_str());
        std::fflush(stdout);
        m_lineOpen = true;
    }

    void showResult(Catch::SectionStats const& stats) {
        const std::string& name   = stats.sectionInfo.name;
        const std::size_t  failed = stats.assertions.failed;
        if (m_lineOpen) std::printf("\r");
        std::printf("  %s   %-*s %5.1f s", failed ? "KO" : "OK", kNameWidth, name.c_str(),
                    stats.durationInSeconds);
        if (failed) std::printf("   (%zu failed)", failed);
        std::printf("\n");
        std::fflush(stdout);
        m_lineOpen = false;
    }

    int  m_depth        = 0;
    bool m_caseReported = false;   // a section of the current test case run already got its line
    bool m_lineOpen     = false;   // a ".." line is on screen waiting for its result
};

} // namespace

CATCH_REGISTER_LISTENER(E2EProgress)
