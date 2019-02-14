cmake_minimum_required (VERSION 2.6)
project(cython-project-1)
add_executable(compute_statistics compute_statistics_main.c compute_statistics.c)
target_link_libraries(compute_statistics curl gsl gslcblas)
target_compile_options(compute_statistics PUBLIC -O3 -Wall -Wextra -pedantic -std=c11)

add_executable(stats_fastcgi.fcgi stats_fastcgi.c compute_statistics.c)
target_link_libraries(stats_fastcgi.fcgi fcgi curl gsl gslcblas)
target_compile_options(stats_fastcgi.fcgi PUBLIC -O3 -Wall -Wextra -pedantic -std=c11)
