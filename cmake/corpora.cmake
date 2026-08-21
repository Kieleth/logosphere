# Corpora: the vendored source text games read, and the one thing they
# share.
#
# A corpus is bytes as published. Nothing in it has been transformed,
# extracted, judged or compiled; that is exactly why two games may read
# the same one without either owning the other. Everything a game's code
# has touched (schemas, seeds, generated headers, extractors, tests)
# belongs to that game alone.
#
# THE RULE THIS ENFORCES: a game DECLARES the corpus it reads, by name.
# It never derives the path from its own directory. Deriving is how the
# Cepheus SRD came to live inside examples/logovger/srd/cepheus, which
# made a second game reading the same book a change to the first game's
# tree.
#
# A THIRD GAME IS THE NORMAL CASE. It calls logosphere_game_corpus with
# the same corpus name another game used and gets the same bytes. There
# is nothing to add: no new define to invent, no path to copy, no
# directory to duplicate. A corpus nobody has vendored yet is a new
# directory under corpora/ and nothing else.

# Where every corpus lives. One directory, at the repo root, outside
# every game.
set(LOGOSPHERE_CORPORA_DIR "${CMAKE_CURRENT_LIST_DIR}/../corpora"
    CACHE INTERNAL "Root of the vendored source corpora")

# logosphere_game_corpus(<target> <corpus-name> <define-prefix>)
#
# Declares that <target> reads the corpus <corpus-name>, and hands the
# target its absolute root as <define-prefix>_CORPUS_DIR. That define is
# the ONLY way game code may learn where the text is; a path built from
# the game's own directory is the defect this replaces.
#
# Mirrors the shape of <PREFIX>_GAME_DIR, which is how a game already
# finds its own data, so a reader who knows one knows the other.
#
# An unknown corpus name is a configure-time failure, not a runtime one.
# A game that silently reads no text would pass its build and fail at
# the first citation, where the reason is much harder to see.
function(logosphere_game_corpus target corpus_name define_prefix)
    get_filename_component(_corpus_root
        "${LOGOSPHERE_CORPORA_DIR}/${corpus_name}" ABSOLUTE)
    if(NOT IS_DIRECTORY "${_corpus_root}")
        message(FATAL_ERROR
            "target '${target}' declares corpus '${corpus_name}', which is "
            "not vendored: ${_corpus_root} does not exist. Corpora live "
            "under corpora/ at the repository root and are shared between "
            "games by name.")
    endif()
    target_compile_definitions(${target} PRIVATE
        ${define_prefix}_CORPUS_DIR="${_corpus_root}")
endfunction()

# logosphere_corpus_path(<out-var> <corpus-name>)
#
# The same declaration for things that are not a target: a ctest command
# line, a script argument. Same name, same bytes, same single source of
# truth for where corpora live.
function(logosphere_corpus_path out_var corpus_name)
    get_filename_component(_corpus_root
        "${LOGOSPHERE_CORPORA_DIR}/${corpus_name}" ABSOLUTE)
    if(NOT IS_DIRECTORY "${_corpus_root}")
        message(FATAL_ERROR
            "corpus '${corpus_name}' is not vendored: ${_corpus_root} "
            "does not exist.")
    endif()
    set(${out_var} "${_corpus_root}" PARENT_SCOPE)
endfunction()
