if (${BUILD_JNI_INTERFACE} OR $ENV{BUILD_JNI_INTERFACE})
    set(BUILD_JNI_INTERFACE 1)
endif ()

if (${BUILD_JNI_INTERFACE})
    # http://public.kitware.com/pipermail/cmake/2012-June/050674.html
    macro(header_directories base_path return_list)
        file(GLOB_RECURSE new_list ${base_path}/*.h)
        set(dir_list "")
        foreach (file_path ${new_list})
            get_filename_component(dir_path ${file_path} DIRECTORY)
            set(dir_list ${dir_list} ${dir_path})
        endforeach ()
        list(REMOVE_DUPLICATES dir_list)
        set(${return_list} ${dir_list})
    endmacro()
    # http://stackoverflow.com/questions/28070810/cmake-generate-error-on-windows-as-it-uses-as-escape-seq
    set(JAVA_HOME $ENV{JAVA_HOME})
    string(REPLACE "\\" "/" JAVA_HOME "${JAVA_HOME}")
    set(JAVA_BASE_INCLUDE_PATH ${JAVA_HOME}/include)
    header_directories(${JAVA_BASE_INCLUDE_PATH} JAVA_FULL_INCLUDE_PATH)
    set(JAVA_LIBRARY_PATH ${JAVA_HOME}/lib)
    set(LIBCGSS_JNI_INTF_FILES src/lib/jni/cgss_jni.h src/lib/jni/cgss_jni.cpp src/lib/jni/jni_helper.hpp)
    set(LIBCGSS_JNI_SOURCE_FILES ${LIBCGSS_SOURCE_FILES} ${LIBCGSS_JNI_INTF_FILES})
    include_directories(${JAVA_FULL_INCLUDE_PATH})
    link_directories(${JAVA_LIBRARY_PATH})
endif ()
