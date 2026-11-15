# Release profile: the default. Full optimization; NDEBUG drops assert()
# (the code keeps its critical invariants as explicit aborts, so this is
# safe — see machine.cpp).
add_compile_options(-O3 -DNDEBUG)
