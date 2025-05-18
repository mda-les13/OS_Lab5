cmake_minimum_required(VERSION 3.14)
project(Server_Test)

include(FetchContent)
	FetchContent_Declare(
		googletest
		# Specify the commit you depend on and update it regularly.
		URL https://github.com/google/googletest/archive/refs/heads/master.zip
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	)
	# For Windows: Prevent overriding the parent project's compiler/linker settings
	set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
	FetchContent_MakeAvailable(googletest)

# link against gtest or gtest_main as needed. Eg
add_executable(${PROJECT_NAME} tests.cpp)
target_link_libraries(${PROJECT_NAME}
	gtest_main
)

add_test(NAME tests COMMAND ${PROJECT_NAME})

# post build command
add_custom_command(
     TARGET ${PROJECT_NAME}  
     COMMENT "Run tests"
     POST_BUILD 
     COMMAND ${PROJECT_NAME}
)