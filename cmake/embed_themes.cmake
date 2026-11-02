# Bakes every themes/*.yaml into a C++ header (run at build time, re-run when a theme changes).
#   cmake -DTHEMES_DIR=<dir> -DOUT=<header> -P embed_themes.cmake
file(GLOB files "${THEMES_DIR}/*.yaml")
list(SORT files)
set(body "// generated from themes/*.yaml - do not edit\n#pragma once\nstruct EmbeddedTheme { const char* file; const char* yaml; };\nstatic const EmbeddedTheme kEmbeddedThemes[] = {\n")
foreach(f ${files})
  get_filename_component(name "${f}" NAME_WE)
  file(READ "${f}" text)
  string(APPEND body "    {\"${name}\", R\"theme_yaml(${text})theme_yaml\"},\n")
endforeach()
string(APPEND body "};\nstatic const int kEmbeddedThemeCount = sizeof(kEmbeddedThemes) / sizeof(kEmbeddedThemes[0]);\n")
# write only when changed, so the exe isn't rebuilt for nothing
if(EXISTS "${OUT}")
  file(READ "${OUT}" old)
  if(old STREQUAL body)
    return()
  endif()
endif()
file(WRITE "${OUT}" "${body}")
