# Development patches

## juce-webview-inspectable.patch

Enables WKWebView inspection in Release builds on macOS 13.3+, so Safari's
Develop menu can attach to the running plugin's interface while iterating on
the HTML/CSS/JS. Apply locally when needed:

    cd external/JUCE && git apply ../../docs/dev/juce-webview-inspectable.patch

Deliberately NOT applied in the repository or in CI: released plugins should
not expose an inspectable WebView, and the submodule stays pristine upstream
JUCE so builds are reproducible. Revert with `git checkout --` on the file
before updating the submodule.
