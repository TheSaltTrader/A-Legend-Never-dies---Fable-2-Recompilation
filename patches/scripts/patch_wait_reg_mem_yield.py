"""s73 - wait_reg_mem_yield_ms (int, 2): how long a WAIT_REG_MEM packet is
polled with a yield before it sleeps for the packet's own interval (vsync on
only; vsync off always yields). The market profile (2026-09-14, 47 fps,
ultrawide, FOV 75) had the GPU command thread asleep in this packet 12% of
its time and the game's 3D thread spinning for the GPU meanwhile: the
first two milliseconds were not enough for the game's in-frame waits at
7,000 draws. Set through FABLE2_TUNE for the A/B; a large value makes every
in-frame wait a poll, a loading screen or an idle menu still sleeps once
its wait outlives the window. APPLY ONCE."""
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\command_processor.cpp"
s = open(P, encoding="utf-8").read()
assert "wait_reg_mem_yield_ms" not in s, "already applied"

def rep(old, new):
    global s
    assert s.count(old) == 1, ("expected 1, found %d: %s" % (s.count(old), old[:90]))
    s = s.replace(old, new)

# the cvar, next to the first REXCVAR_DEFINE in the file
first = s.index("REXCVAR_DEFINE_")
line_start = s.rfind("\n", 0, first) + 1
s = s[:line_start] + '''REXCVAR_DEFINE_INT32(wait_reg_mem_yield_ms, 2, "GPU",
                     "With vsync: poll a WAIT_REG_MEM packet with a yield for this many ms before sleeping "
                     "for its own interval (in-frame waits are short; a load screen's are not)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
''' + s[line_start:]

rep('''        const bool fresh = std::chrono::steady_clock::now() - wait_started <
                           std::chrono::milliseconds(2);
''', '''        const bool fresh =
            std::chrono::steady_clock::now() - wait_started <
            std::chrono::milliseconds(std::max<int32_t>(REXCVAR_GET(wait_reg_mem_yield_ms), 0));
''')
open(P, "w", encoding="utf-8", newline="").write(s)
print("patched: wait_reg_mem_yield_ms")
