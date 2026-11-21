# Injected into the engine's configure step by Tools/build.py (CMAKE_PROJECT_INCLUDE).
# The engine bakes its absolute source path into CE_ENGINE_ROOT. For a portable zip we want ".",
# so engine data is found relative to the working directory, which cerender.py sets to the package root.
# Compile options come after compile definitions on the command line, so this wins.
# Remove once the engine resolves its root at runtime.
add_compile_options(-UCE_ENGINE_ROOT [=[-DCE_ENGINE_ROOT="."]=])
