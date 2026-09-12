@echo off
rem Test launch for the magenta tree impostors (2026-09-12): the same game,
rem with the shader memory-export readback turned back ON for this process
rem only (the 60 fps lock turns it off; the frame rate will drop while this
rem runs). Go to the forest and look at the distant trees. Nothing is saved.
rem
rem   tools\impostor_test_memexport.cmd
set "FABLE2_TUNE=readback_memexport=true"
call "%~dp0run.cmd" %*
