add_executable(llama-gui WIN32
    main.cpp
)

target_link_libraries(llama-gui PRIVATE llama)

target_compile_features(llama-gui PRIVATE cxx_std_17)
