#pragma once
#include <string>
#include <unordered_map>

// Case-insensitive word-level snippet substitution engine.
// Snippets are loaded from config; applied after transcription.
class SnippetEngine {
public:
    void setSnippets(const std::unordered_map<std::string, std::string>& snippets) {
        m_snippets = snippets;
    }

    // Returns a copy of `text` with all trigger phrases replaced by their
    // expansion values. Matching is case-insensitive, longest-first.
    std::string apply(const std::string& text) const;

private:
    std::unordered_map<std::string, std::string> m_snippets;
};

// Detects whether the currently focused window belongs to a code editor.
// Returns AppMode::CODING for VS Code, Cursor, nvim, Windows Terminal, etc.
// Falls back to AppMode::PROSE for everything else.
#include "formatter.h"   // for AppMode
AppMode DetectModeFromActiveWindow();
