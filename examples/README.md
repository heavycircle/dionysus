# Examples

Here are examples using the DINO SDK. These examples primarily show when the API should be used over standard `glibc` variants.

| **Category** | **Location**                                | **Description**                            |
| ------------ | ------------------------------------------- | ------------------------------------------ |
| Big Files    | [`fs_example`](./fs_example/main.c)         | Wrapped API for getting big file size      |
| Heap         | [`heap_example`](./heap_example/main.c)     | Showing use of standard GNU heap           |
| Sleep        | [`sleep_example`](./sleep_example/main.c)   | Wrapped `sleep` for microseconds           |
| Threading    | [`thread_example`](./thread_example/main.c) | Wrapped threading for size and performance |
