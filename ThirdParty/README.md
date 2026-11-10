# Third-party dependencies

`glfw/` is the unmodified GLFW 3.5.1 tag archive, including headers, source, upstream build files and the [zlib/libpng license](glfw/LICENSE.md).

- Upstream: https://github.com/glfw/glfw/tree/3.5.1
- Archive: https://codeload.github.com/glfw/glfw/zip/refs/tags/3.5.1
- Downloaded archive SHA-256: `e9a80355e8a0c59b15ae8576c2c3aeae792c2b1082ec426dc93bde70d5017fda`

The game statically links the upstream Windows source set listed in `Tutorial/GlfwSources.props`. No downloaded SDK package, third-party DLL, or local library path is needed at build or runtime. Recheck the upstream source list when updating the version.
