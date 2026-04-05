# fastjsondiff

`fastjsondiff` is a Python package for high-performance JSON diffing inspired by [`jsondiff`](https://github.com/xlwings/jsondiff).
It is implemented in C++ using nanobind to make python bindings.

## Status

- Recursive diff and similarity implemented in C++
- JSON/YAML loaders, dumpers, marshaling, and CLI support

## Development

```bash
python3 -m pip install -e . pytest
pytest
```
