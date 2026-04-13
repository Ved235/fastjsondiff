# cjsondiff

`cjsondiff` is a Python package for high-performance JSON diffing inspired by [`jsondiff`](https://github.com/xlwings/jsondiff).
It is implemented in C++ using nanobind to make python bindings.
<img alt="image" src="https://github.com/user-attachments/assets/09cd370f-4725-4f80-b0f1-b856a979646c" />



## Status

- Recursive diff and similarity implemented in C++
- JSON/YAML loaders, dumpers, marshaling, and CLI support

## Development

```bash
python3 -m pip install -e . pytest
pytest
```
