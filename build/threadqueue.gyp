{
    "targets": [
        {
            "target_name": "threadqueue",
            "type": "static_library",
            "ldflags": [
                "-lpthread",
            ],
            "include_dirs": [
                "<(DEPTH)/third_party/threadqueue",
            ],
            "sources": [
                "<(DEPTH)/third_party/threadqueue/threadqueue.c",
            ],
        },
    ],
}
