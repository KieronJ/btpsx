# btpsx
A WIP port of rpsx to integrate a JIT binary translator

## Config options
btpsx expects a **config.json** file alongside the executable.

The following options are supported:

| Option | Description | Default |
| ------------- | ------------- | ------------- |
| bios (required) | Path to the bios file | N/A |
| disc (required) | Path to the game's disc file | N/A |
| enable_audio (bool) | Enables/disables audio | false |
| log_level | Sets the spdlog logging level (off/trace/debug/info/warn/err/critical) | debug |
