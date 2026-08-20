# Install the repository's git hooks at configure time.
#
# The hooks (.githooks/pre-rebase, .githooks/pre-push) enforce the git
# integration policy in CLAUDE.md: never rebase, never force-push, never
# push to main. They live in a tracked directory because .git/hooks is
# not tracked and therefore never reaches a clone, which is precisely how
# a policy that existed only in the owner's head reached no agent session
# and cost three merged pull requests.
#
# Wiring it to `cmake -S . -B build` means the README's first command is
# the install step. No manual "also run this" line to forget.
#
# Skipped when the engine is consumed via FetchContent (we configure our
# own clone, not somebody else's) and when there is no .git at all.

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git"
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-git-hooks.sh")
        execute_process(
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-git-hooks.sh"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _logosphere_hooks_out
            ERROR_VARIABLE  _logosphere_hooks_err
            RESULT_VARIABLE _logosphere_hooks_rc
        )
        if(_logosphere_hooks_rc EQUAL 0)
            string(STRIP "${_logosphere_hooks_out}" _logosphere_hooks_out)
            message(STATUS "${_logosphere_hooks_out}")
        else()
            # Never fail the configure over this. A read-only checkout or
            # an exotic git setup is not a build error.
            message(STATUS "git hooks not installed: ${_logosphere_hooks_err}")
        endif()
    endif()
endif()
