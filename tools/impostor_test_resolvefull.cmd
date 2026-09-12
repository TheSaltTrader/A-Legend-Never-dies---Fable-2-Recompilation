@echo off
rem Test launch for the magenta tree impostors (2026-09-12): the same game,
rem with the resolve readback (the "Black texture fix") set to FULL for this
rem process only, whatever the settings say. Costs frame rate. Go to the
rem forest and look at the distant trees. Nothing is saved.
rem
rem   tools\impostor_test_resolvefull.cmd
set "FABLE2_TUNE=readback_resolve=full"
call "%~dp0run.cmd" %*
