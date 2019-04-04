cmake_minimum_required (VERSION 2.6)
project(cython-project-1)

add_executable(compute_statistics compute_statistics_main.c compute_statistics.c)
target_link_libraries(compute_statistics curl uv gsl gslcblas pthread sds)
target_compile_options(compute_statistics PUBLIC -O3 -Wall -Wextra -pedantic -std=c11)

add_executable(stats_fastcgi.fcgi stats_fastcgi.c compute_statistics.c)
target_link_libraries(stats_fastcgi.fcgi fcgi curl gsl gslcblas pthread)
target_compile_options(stats_fastcgi.fcgi PUBLIC -O3 -Wall -Wextra -pedantic)

add_library(stats_uwsgi SHARED stats_uwsgi.c compute_statistics.c)
target_link_libraries(stats_uwsgi curl gsl gslcblas pthread)
target_compile_options(stats_uwsgi PUBLIC -O2 -I -Wall -Wextra -pedantic -std=c11 -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -DUWSGI_HAS_IFADDRS -DUWSGI_ZLIB -DUWSGI_LOCK_USE_MUTEX -DUWSGI_EVENT_USE_EPOLL -DUWSGI_EVENT_TIMER_USE_TIMERFD -DUWSGI_EVENT_FILEMONITOR_USE_INOTIFY  -DUWSGI_PCRE -DUWSGI_ROUTING -DUWSGI_VERSION="\"2.0.18\"" -DUWSGI_VERSION_BASE="2" -DUWSGI_VERSION_MAJOR="0" -DUWSGI_VERSION_MINOR="18" -DUWSGI_VERSION_REVISION="0" -DUWSGI_VERSION_CUSTOM="\"\"" -DUWSGI_YAML -I/usr/local/include -DUWSGI_JSON -DUWSGI_SSL -I/usr/local/include/libxml2 -DUWSGI_XML -DUWSGI_XML_LIBXML2)
