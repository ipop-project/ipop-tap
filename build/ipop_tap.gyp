{
    "target_defaults": {
        "default_configuration": "release",
        "cflags": ["--std=gnu99", "-Wall"],
        "configurations": {
            "debug": {
                "defines": ["DEBUG"],
                "cflags": ["-g", "-O3"],
            },
            "release": {
                "cflags": ["-O3"],
            },
        },
    },
    "targets": [
        {
            "target_name": "ipop_tap",
            "type": "executable",
            "include_dirs": [
                "<(DEPTH)/include",
                "<(DEPTH)/third_party/klib",
                "<(DEPTH)/third_party/threadqueue",
            ],
            "conditions": [
                ["OS=='android'", {
                    "defines": ["DROID_BUILD"],
                }],
            ],
            "ldflags": [
                "-lpthread",
                "-ljansson",
            ],
            "dependencies": [
                "threadqueue.gyp:threadqueue",
            ],
            "sources": ["<!@(find ../src -name *.c)"],
        },
    ],
}
