#ifndef WORDPASS_BUILD_INFO_H
#define WORDPASS_BUILD_INFO_H

// Single compile-time identity for screenshots, support and ROM comparisons.
// The Makefile supplies a short Git SHA, with a deterministic archive fallback.
#ifndef WP_BUILD_REV
#define WP_BUILD_REV "source"
#endif
#define WP_BUILD_LABEL "BUILD " WP_BUILD_REV
#if defined(WP_CJK_MAIN)
#define WP_BUILD_FLAVOR "16 LANG"
#elif defined(WP_CYR_MAIN)
#define WP_BUILD_FLAVOR "13 LANG"
#elif defined(WP_CYR_PREVIEW)
#define WP_BUILD_FLAVOR "CYR DEMO"
#else
#define WP_BUILD_FLAVOR "RETAIL"
#endif

#endif
