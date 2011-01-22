IF (NOT DOT_EXECUTABLE_FOUND)
  FIND_PROGRAM(DOT_EXECUTABLE
    NAMES dot
    PATHS 
      # UNIX paths
      "/bin"
      "/usr/bin"
      "/usr/local/bin"
      "/opt/bin"
      "/opt/local/bin"
      # Windows paths
      "$ENV{ProgramFiles}/Graphviz 2.21/bin"
      "C:/Program Files/Graphviz 2.21/bin"
      "$ENV{ProgramFiles}/ATT/Graphviz/bin"
      "C:/Program Files/ATT/Graphviz/bin"
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\ATT\\Graphviz;InstallPath]/bin
      # Mac OS X Bundle paths
      /Applications/Graphviz.app/Contents/MacOS
      /Applications/Doxygen.app/Contents/Resources
      /Applications/Doxygen.app/Contents/MacOS
    DOC "Graphviz Dot tool for generating image graph from dot file"
  )
  IF (DOT_EXECUTABLE)
    SET (DOT_EXECUTABLE_FOUND TRUE)
    MESSAGE(STATUS "Dot executable found : ${DOT_EXECUTABLE}")
  ENDIF (DOT_EXECUTABLE)
ENDIF (NOT DOT_EXECUTABLE_FOUND)
