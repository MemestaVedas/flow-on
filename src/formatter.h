#pragma once
#include <string>

// Application mode — determines formatter behaviour and coding transforms.
enum class AppMode { PROSE, CODING };

// Four-pass formatter:
//   1. Strip universal fillers (um, uh, …)
//   2. Strip sentence-start fillers (so, well, …) — anchored to ^ only
//   3. Cleanup whitespace / leading punct / capitalise
//   4. Fix trailing punctuation
//   5. (CODING only) Apply coding transforms (camel / snake / all caps)
std::string FormatTranscription(const std::string& raw, AppMode mode);
