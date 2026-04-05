import copy

import pytest

import jsondiff as upstream_jsondiff
from fastjsondiff import jsondiff as fast_jsondiff


def _build_nested_case(section_count=24, items_per_section=60, mutate_stride=3):
    left = {
        f"section_{i}": {
            "items": [
                {
                    "id": j,
                    "name": f"item-{i}-{j}",
                    "values": [j, j * 2, {"flag": j % 3 == 0, "score": j / 7}],
                }
                for j in range(items_per_section)
            ],
            "meta": {
                "enabled": i % 2 == 0,
                "threshold": i * 3,
                "tags": {f"tag-{i}", f"group-{i % 5}"},
            },
        }
        for i in range(section_count)
    }
    right = copy.deepcopy(left)
    score_index = min(10, items_per_section - 1)
    delete_index = min(20, items_per_section - 1)
    for i in range(0, section_count, mutate_stride):
        right[f"section_{i}"]["meta"]["threshold"] += 11
        right[f"section_{i}"]["meta"]["tags"].add("hot")
        right[f"section_{i}"]["items"][score_index]["values"][2]["score"] += 1.25
        right[f"section_{i}"]["items"].insert(
            5,
            {"id": 1000 + i, "name": f"new-{i}", "values": [1, 2, {"flag": True, "score": 9.9}]},
        )
        del right[f"section_{i}"]["items"][delete_index]
    removed_section = min(5, section_count - 1)
    del right[f"section_{removed_section}"]
    right["section_new"] = {
        "items": [{"id": 1, "name": "fresh", "values": [7, 8, {"flag": False, "score": 4.2}]}],
        "meta": {"enabled": True, "threshold": 77, "tags": {"new"}},
    }
    return left, right


def _build_list_case(list_size=350, mutate_stride=17, insert_count=10, delete_stop=150, delete_stride=19):
    left = [
        {
            "idx": i,
            "payload": {
                "name": f"user-{i}",
                "active": i % 2 == 0,
                "history": [i, i + 1, i + 2],
            },
        }
        for i in range(list_size)
    ]
    right = copy.deepcopy(left)
    for i in range(0, list_size, mutate_stride):
        right[i]["payload"]["active"] = not right[i]["payload"]["active"]
        right[i]["payload"]["history"][1] += 100
    for offset in range(insert_count):
        right.insert(25 * offset + 3, {"idx": 9000 + offset, "payload": {"name": "extra", "active": True, "history": [1, 2, 3]}})
    for idx in sorted(range(15, min(delete_stop, list_size), delete_stride), reverse=True):
        del right[idx]
    return left, right


def _build_set_case(primary_size=200, secondary_size=150, nested_size=100):
    left = {
        "primary": {f"user-{i}" for i in range(primary_size)},
        "secondary": {i for i in range(secondary_size)},
        "nested": {
            "alpha": {f"a-{i}" for i in range(nested_size)},
            "beta": {f"b-{i}" for i in range(nested_size)},
        },
    }
    right = copy.deepcopy(left)
    right["primary"].difference_update({f"user-{i}" for i in range(max(20, primary_size // 10))})
    right["primary"].update({f"user-new-{i}" for i in range(max(30, primary_size // 8))})
    right["secondary"].difference_update(set(range(10, min(35, secondary_size // 4 + 10))))
    right["secondary"].update({500 + i for i in range(max(25, secondary_size // 6))})
    right["nested"]["alpha"].discard("a-2")
    right["nested"]["alpha"].add(f"a-{nested_size * 10}")
    right["nested"]["beta"].update({f"b-{nested_size * 2}", f"b-{nested_size * 2 + 1}"})
    return left, right


DIFF_CASES = {
    "nested_dict": _build_nested_case(),
    "nested_dict_large": _build_nested_case(section_count=48, items_per_section=120, mutate_stride=4),
    "list_lcs": _build_list_case(),
    "list_lcs_large": _build_list_case(list_size=800, mutate_stride=19, insert_count=24, delete_stop=420, delete_stride=23),
    "set_heavy": _build_set_case(),
    "set_heavy_large": _build_set_case(primary_size=800, secondary_size=600, nested_size=350),
}

IMPLEMENTATIONS = [
    pytest.param(upstream_jsondiff, id="jsondiff"),
    pytest.param(fast_jsondiff, id="fastjsondiff"),
]


@pytest.mark.parametrize("impl", IMPLEMENTATIONS)
@pytest.mark.parametrize("case_name", sorted(DIFF_CASES))
def test_benchmark_diff(benchmark, impl, case_name):
    left, right = DIFF_CASES[case_name]
    benchmark(impl.diff, left, right)
