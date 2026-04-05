#include <Python.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

namespace nb = nanobind;

struct Symbol {
    std::string label;
};

enum class SyntaxKind {
    Compact,
    Symmetric,
    Explicit,
    RightOnly,
};

struct Symbols {
    nb::object missing;
    nb::object identical;
    nb::object delete_;
    nb::object insert;
    nb::object update;
    nb::object add;
    nb::object discard;
    nb::object replace;
    nb::object left;
    nb::object right;
};

struct DiffResult {
    nb::object diff;
    double similarity;
};

struct CompactJsonDiffSyntax {};
struct ExplicitJsonDiffSyntax {};
struct SymmetricJsonDiffSyntax {};
struct RightOnlyJsonDiffSyntax {};

static Symbols &g_symbols_ref() {
    static Symbols *value = new Symbols();
    return *value;
}

static nb::object &g_default_loader_ref() {
    static nb::object *value = new nb::object();
    return *value;
}

static nb::object &g_default_dumper_ref() {
    static nb::object *value = new nb::object();
    return *value;
}

#define g_symbols g_symbols_ref()
#define g_default_loader g_default_loader_ref()
#define g_default_dumper g_default_dumper_ref()

static bool py_equal(nb::handle a, nb::handle b) {
    int result = PyObject_RichCompareBool(a.ptr(), b.ptr(), Py_EQ);
    if (result < 0) {
        throw nb::python_error();
    }
    return result == 1;
}

static Py_ssize_t py_len(nb::handle obj) {
    Py_ssize_t result = PyObject_Length(obj.ptr());
    if (result < 0) {
        throw nb::python_error();
    }
    return result;
}

static bool is_dict(nb::handle obj) {
    return PyDict_Check(obj.ptr()) != 0;
}

static bool is_list(nb::handle obj) {
    return PyList_Check(obj.ptr()) != 0;
}

static bool is_tuple(nb::handle obj) {
    return PyTuple_Check(obj.ptr()) != 0;
}

static bool is_list_like(nb::handle obj) {
    return is_list(obj) || is_tuple(obj);
}

static bool is_set(nb::handle obj) {
    return PyAnySet_Check(obj.ptr()) != 0;
}

static nb::dict copy_dict(nb::handle obj) {
    PyObject *copy = PyDict_Copy(obj.ptr());
    if (copy == nullptr) {
        throw nb::python_error();
    }
    return nb::steal<nb::dict>(copy);
}

static nb::list copy_list(nb::handle obj) {
    PyObject *copy = PySequence_List(obj.ptr());
    if (copy == nullptr) {
        throw nb::python_error();
    }
    return nb::steal<nb::list>(copy);
}

static nb::tuple list_to_tuple(nb::handle obj) {
    PyObject *tuple = PyList_AsTuple(obj.ptr());
    if (tuple == nullptr) {
        throw nb::python_error();
    }
    return nb::steal<nb::tuple>(tuple);
}

static nb::set copy_set(nb::handle obj) {
    PyObject *copy = PySet_New(obj.ptr());
    if (copy == nullptr) {
        throw nb::python_error();
    }
    return nb::steal<nb::set>(copy);
}

static nb::tuple make_tuple2(nb::handle first, nb::handle second) {
    PyObject *tuple = PyTuple_Pack(2, first.ptr(), second.ptr());
    if (tuple == nullptr) {
        throw nb::python_error();
    }
    return nb::steal<nb::tuple>(tuple);
}

static nb::list make_list2(nb::handle first, nb::handle second) {
    PyObject *list = PyList_New(2);
    if (list == nullptr) {
        throw nb::python_error();
    }
    Py_INCREF(first.ptr());
    Py_INCREF(second.ptr());
    PyList_SET_ITEM(list, 0, first.ptr());
    PyList_SET_ITEM(list, 1, second.ptr());
    return nb::steal<nb::list>(list);
}

static void dict_set(nb::dict &d, nb::handle key, nb::handle value) {
    if (PyDict_SetItem(d.ptr(), key.ptr(), value.ptr()) != 0) {
        throw nb::python_error();
    }
}

static void list_append(nb::list &l, nb::handle value) {
    if (PyList_Append(l.ptr(), value.ptr()) != 0) {
        throw nb::python_error();
    }
}

static nb::object dict_get(nb::handle d, nb::handle key) {
    PyObject *value = PyDict_GetItemWithError(d.ptr(), key.ptr());
    if (value == nullptr) {
        if (PyErr_Occurred() != nullptr) {
            throw nb::python_error();
        }
        return nb::none();
    }
    return nb::borrow<nb::object>(value);
}

static bool dict_contains(nb::handle d, nb::handle key) {
    int result = PyDict_Contains(d.ptr(), key.ptr());
    if (result < 0) {
        throw nb::python_error();
    }
    return result == 1;
}

static std::vector<nb::object> dict_keys(nb::handle d) {
    std::vector<nb::object> keys;
    PyObject *key = nullptr;
    PyObject *value = nullptr;
    Py_ssize_t pos = 0;
    while (PyDict_Next(d.ptr(), &pos, &key, &value)) {
        keys.push_back(nb::borrow<nb::object>(key));
    }
    return keys;
}

static nb::list dict_keys_list(nb::handle d) {
    nb::list result;
    PyObject *key = nullptr;
    PyObject *value = nullptr;
    Py_ssize_t pos = 0;
    while (PyDict_Next(d.ptr(), &pos, &key, &value)) {
        list_append(result, nb::handle(key));
    }
    return result;
}

static void dict_update_items(nb::dict &target, nb::handle source) {
    PyObject *key = nullptr;
    PyObject *value = nullptr;
    Py_ssize_t pos = 0;
    while (PyDict_Next(source.ptr(), &pos, &key, &value)) {
        dict_set(target, nb::handle(key), nb::handle(value));
    }
}

static std::vector<nb::object> sequence_to_vector(nb::handle obj) {
    PyObject *seq = PySequence_Fast(obj.ptr(), "expected sequence");
    if (seq == nullptr) {
        throw nb::python_error();
    }
    Py_ssize_t size = PySequence_Fast_GET_SIZE(seq);
    PyObject **items = PySequence_Fast_ITEMS(seq);
    std::vector<nb::object> result;
    result.reserve(static_cast<size_t>(size));
    for (Py_ssize_t i = 0; i < size; ++i) {
        result.push_back(nb::borrow<nb::object>(items[i]));
    }
    Py_DECREF(seq);
    return result;
}

static std::vector<nb::object> iterable_to_vector(nb::handle obj) {
    PyObject *iter = PyObject_GetIter(obj.ptr());
    if (iter == nullptr) {
        throw nb::python_error();
    }
    std::vector<nb::object> result;
    while (PyObject *item = PyIter_Next(iter)) {
        result.push_back(nb::steal<nb::object>(item));
    }
    Py_DECREF(iter);
    if (PyErr_Occurred() != nullptr) {
        throw nb::python_error();
    }
    return result;
}

static nb::set set_from_vector(const std::vector<nb::object> &items) {
    PyObject *set = PySet_New(nullptr);
    if (set == nullptr) {
        throw nb::python_error();
    }
    nb::set result = nb::steal<nb::set>(set);
    for (const auto &item : items) {
        if (PySet_Add(result.ptr(), item.ptr()) != 0) {
            throw nb::python_error();
        }
    }
    return result;
}

static long as_long(nb::handle obj) {
    long value = PyLong_AsLong(obj.ptr());
    if (value == -1 && PyErr_Occurred() != nullptr) {
        throw nb::python_error();
    }
    return value;
}

static nb::object sequence_item(nb::handle obj, Py_ssize_t index) {
    PyObject *item = PySequence_GetItem(obj.ptr(), index);
    if (item == nullptr) {
        throw nb::python_error();
    }
    return nb::steal<nb::object>(item);
}

static void dict_delete_keys(nb::dict &result, nb::handle keys) {
    for (const auto &key : iterable_to_vector(keys)) {
        if (PyDict_DelItem(result.ptr(), key.ptr()) != 0) {
            throw nb::python_error();
        }
    }
}

static void dict_delete_mapping_keys(nb::dict &result, nb::handle mapping) {
    for (const auto &key : dict_keys(mapping)) {
        if (PyDict_DelItem(result.ptr(), key.ptr()) != 0) {
            throw nb::python_error();
        }
    }
}

static void dict_copy_items(nb::dict &result, nb::handle mapping) {
    for (const auto &key : dict_keys(mapping)) {
        dict_set(result, key, dict_get(mapping, key));
    }
}

static void list_delete_positions(nb::list &result, nb::handle positions) {
    for (const auto &pos_obj : iterable_to_vector(positions)) {
        if (PySequence_DelItem(result.ptr(), as_long(pos_obj)) != 0) {
            throw nb::python_error();
        }
    }
}

static void list_delete_pairs(nb::list &result, nb::handle pairs, bool reverse = false) {
    std::vector<nb::object> items = sequence_to_vector(pairs);
    if (reverse) {
        for (auto it = items.rbegin(); it != items.rend(); ++it) {
            if (PySequence_DelItem(result.ptr(), as_long(sequence_item(*it, 0))) != 0) {
                throw nb::python_error();
            }
        }
    } else {
        for (const auto &item : items) {
            if (PySequence_DelItem(result.ptr(), as_long(sequence_item(item, 0))) != 0) {
                throw nb::python_error();
            }
        }
    }
}

static void list_insert_pairs(nb::list &result, nb::handle pairs, bool reverse = false) {
    std::vector<nb::object> items = sequence_to_vector(pairs);
    if (reverse) {
        for (auto it = items.rbegin(); it != items.rend(); ++it) {
            nb::object index = sequence_item(*it, 0);
            nb::object value = sequence_item(*it, 1);
            if (PyList_Insert(result.ptr(), as_long(index), value.ptr()) != 0) {
                throw nb::python_error();
            }
        }
    } else {
        for (const auto &item : items) {
            nb::object index = sequence_item(item, 0);
            nb::object value = sequence_item(item, 1);
            if (PyList_Insert(result.ptr(), as_long(index), value.ptr()) != 0) {
                throw nb::python_error();
            }
        }
    }
}

template <typename Recur>
static void patch_list_items(nb::list &result, nb::handle d, Recur recur) {
    for (const auto &key : dict_keys(d)) {
        if (py_equal(key, g_symbols.delete_) || py_equal(key, g_symbols.insert)) {
            continue;
        }
        long index = as_long(key);
        nb::object current = sequence_item(result, index);
        nb::object patched = recur(current, dict_get(d, key));
        if (PySequence_SetItem(result.ptr(), index, patched.ptr()) != 0) {
            throw nb::python_error();
        }
    }
}

static void set_discard_all(nb::set &result, nb::handle values) {
    for (const auto &item : iterable_to_vector(values)) {
        if (PySet_Discard(result.ptr(), item.ptr()) < 0) {
            throw nb::python_error();
        }
    }
}

static void set_add_all(nb::set &result, nb::handle values) {
    for (const auto &item : iterable_to_vector(values)) {
        if (PySet_Add(result.ptr(), item.ptr()) != 0) {
            throw nb::python_error();
        }
    }
}

static std::string obj_to_string(nb::handle obj) {
    return nb::cast<std::string>(nb::str(obj));
}

static std::string join_path(const std::string &path, nb::handle key) {
    std::string key_string = obj_to_string(key);
    if (path.empty()) {
        return key_string;
    }
    return path + "." + key_string;
}

static SyntaxKind parse_syntax_name(const std::string &syntax) {
    if (syntax == "compact") {
        return SyntaxKind::Compact;
    }
    if (syntax == "symmetric") {
        return SyntaxKind::Symmetric;
    }
    if (syntax == "explicit") {
        return SyntaxKind::Explicit;
    }
    if (syntax == "rightonly") {
        return SyntaxKind::RightOnly;
    }
    std::string message = "Unsupported syntax: " + syntax;
    throw nb::value_error(message.c_str());
}

static SyntaxKind syntax_from_object(nb::handle obj) {
    if (PyUnicode_Check(obj.ptr())) {
        return parse_syntax_name(nb::cast<std::string>(nb::str(obj)));
    }
    if (nb::isinstance<CompactJsonDiffSyntax>(obj)) {
        return SyntaxKind::Compact;
    }
    if (nb::isinstance<ExplicitJsonDiffSyntax>(obj)) {
        return SyntaxKind::Explicit;
    }
    if (nb::isinstance<SymmetricJsonDiffSyntax>(obj)) {
        return SyntaxKind::Symmetric;
    }
    if (nb::isinstance<RightOnlyJsonDiffSyntax>(obj)) {
        return SyntaxKind::RightOnly;
    }
    throw nb::type_error("Unsupported syntax object");
}

static nb::object call_python(nb::handle callable, const std::vector<nb::handle> &args, nb::handle kwargs = nb::none()) {
    PyObject *tuple = PyTuple_New(static_cast<Py_ssize_t>(args.size()));
    if (tuple == nullptr) {
        throw nb::python_error();
    }
    for (size_t i = 0; i < args.size(); ++i) {
        Py_INCREF(args[i].ptr());
        PyTuple_SET_ITEM(tuple, static_cast<Py_ssize_t>(i), args[i].ptr());
    }
    PyObject *result = PyObject_Call(callable.ptr(), tuple, kwargs.is_none() ? nullptr : kwargs.ptr());
    Py_DECREF(tuple);
    if (result == nullptr) {
        throw nb::python_error();
    }
    return nb::steal<nb::object>(result);
}

static nb::object emit_value_diff(SyntaxKind syntax, nb::handle a, nb::handle b, double similarity) {
    if (similarity == 1.0) {
        return nb::dict();
    }
    if (syntax == SyntaxKind::Symmetric) {
        return make_list2(a, b);
    }
    if ((syntax == SyntaxKind::Compact || syntax == SyntaxKind::RightOnly) && is_dict(b)) {
        nb::dict result;
        dict_set(result, g_symbols.replace, b);
        return result;
    }
    return nb::borrow<nb::object>(b);
}

static nb::object emit_list_diff(
    SyntaxKind syntax,
    nb::handle a,
    nb::handle b,
    double similarity,
    nb::list inserted,
    nb::dict changed,
    nb::list deleted_positions,
    nb::list deleted_pairs
) {
    if (syntax == SyntaxKind::RightOnly) {
        if (similarity == 1.0) {
            return nb::dict();
        }
        return nb::borrow<nb::object>(b);
    }

    if (syntax == SyntaxKind::Compact) {
        if (similarity == 0.0) {
            if (is_dict(b)) {
                nb::dict result;
                dict_set(result, g_symbols.replace, b);
                return result;
            }
            return nb::borrow<nb::object>(b);
        }
        if (similarity == 1.0 && py_len(inserted) == 0 && py_len(changed) == 0 && py_len(deleted_positions) == 0) {
            return nb::dict();
        }
        nb::dict result = copy_dict(changed);
        if (py_len(inserted) != 0) {
            dict_set(result, g_symbols.insert, inserted);
        }
        if (py_len(deleted_positions) != 0) {
            dict_set(result, g_symbols.delete_, deleted_positions);
        }
        return result;
    }

    if (syntax == SyntaxKind::Explicit) {
        if (similarity == 0.0 && py_len(inserted) == 0 && py_len(changed) == 0 && py_len(deleted_positions) == 0) {
            return nb::borrow<nb::object>(b);
        }
        if (similarity == 1.0 && py_len(inserted) == 0 && py_len(changed) == 0 && py_len(deleted_positions) == 0) {
            return nb::dict();
        }
        nb::dict result = copy_dict(changed);
        if (py_len(inserted) != 0) {
            dict_set(result, g_symbols.insert, inserted);
        }
        if (py_len(deleted_positions) != 0) {
            dict_set(result, g_symbols.delete_, deleted_positions);
        }
        return result;
    }

    if (similarity == 0.0 && py_len(inserted) == 0 && py_len(changed) == 0 && py_len(deleted_pairs) == 0) {
        return make_list2(a, b);
    }
    if (similarity == 1.0 && py_len(inserted) == 0 && py_len(changed) == 0 && py_len(deleted_pairs) == 0) {
        return nb::dict();
    }
    nb::dict result = copy_dict(changed);
    if (py_len(inserted) != 0) {
        dict_set(result, g_symbols.insert, inserted);
    }
    if (py_len(deleted_pairs) != 0) {
        dict_set(result, g_symbols.delete_, deleted_pairs);
    }
    return result;
}

static nb::object emit_dict_diff(
    SyntaxKind syntax,
    nb::handle a,
    nb::handle b,
    double similarity,
    nb::dict added,
    nb::dict changed,
    nb::dict removed
) {
    if (syntax == SyntaxKind::Compact) {
        if (similarity == 0.0) {
            if (is_dict(b)) {
                nb::dict result;
                dict_set(result, g_symbols.replace, b);
                return result;
            }
            return nb::borrow<nb::object>(b);
        }
        if (similarity == 1.0 && py_len(added) == 0 && py_len(changed) == 0 && py_len(removed) == 0) {
            return nb::dict();
        }
        nb::dict result = copy_dict(changed);
        dict_update_items(result, added);
        if (py_len(removed) != 0) {
            dict_set(result, g_symbols.delete_, dict_keys_list(removed));
        }
        return result;
    }

    if (syntax == SyntaxKind::Explicit) {
        if (similarity == 0.0 && py_len(added) == 0 && py_len(changed) == 0 && py_len(removed) == 0) {
            return nb::borrow<nb::object>(b);
        }
        if (similarity == 1.0 && py_len(added) == 0 && py_len(changed) == 0 && py_len(removed) == 0) {
            return nb::dict();
        }
        nb::dict result;
        if (py_len(added) != 0) {
            dict_set(result, g_symbols.insert, added);
        }
        if (py_len(changed) != 0) {
            dict_set(result, g_symbols.update, changed);
        }
        if (py_len(removed) != 0) {
            dict_set(result, g_symbols.delete_, dict_keys_list(removed));
        }
        return result;
    }

    if (syntax == SyntaxKind::Symmetric) {
        if (similarity == 0.0 && py_len(added) == 0 && py_len(changed) == 0 && py_len(removed) == 0) {
            return make_list2(a, b);
        }
        if (similarity == 1.0 && py_len(added) == 0 && py_len(changed) == 0 && py_len(removed) == 0) {
            return nb::dict();
        }
        nb::dict result = copy_dict(changed);
        if (py_len(added) != 0) {
            dict_set(result, g_symbols.insert, added);
        }
        if (py_len(removed) != 0) {
            dict_set(result, g_symbols.delete_, removed);
        }
        return result;
    }

    if (similarity == 1.0) {
        return nb::dict();
    }
    nb::dict result = copy_dict(changed);
    dict_update_items(result, added);
    if (py_len(removed) != 0) {
        dict_set(result, g_symbols.delete_, dict_keys_list(removed));
    }
    return result;
}

static nb::object emit_set_diff(
    SyntaxKind syntax,
    nb::handle a,
    nb::handle b,
    double similarity,
    nb::set added,
    nb::set removed
) {
    Py_ssize_t removed_size = PySet_GET_SIZE(removed.ptr());
    Py_ssize_t a_size = PySet_GET_SIZE(a.ptr());

    if (syntax == SyntaxKind::Compact) {
        if (similarity == 0.0 || removed_size == a_size) {
            if (is_dict(b)) {
                nb::dict result;
                dict_set(result, g_symbols.replace, b);
                return result;
            }
            return nb::borrow<nb::object>(b);
        }
        nb::dict result;
        if (removed_size != 0) {
            dict_set(result, g_symbols.discard, removed);
        }
        if (PySet_GET_SIZE(added.ptr()) != 0) {
            dict_set(result, g_symbols.add, added);
        }
        return result;
    }

    if (syntax == SyntaxKind::Explicit) {
        if (similarity == 0.0 || removed_size == a_size) {
            return nb::borrow<nb::object>(b);
        }
        nb::dict result;
        if (removed_size != 0) {
            dict_set(result, g_symbols.discard, removed);
        }
        if (PySet_GET_SIZE(added.ptr()) != 0) {
            dict_set(result, g_symbols.add, added);
        }
        return result;
    }

    if (syntax == SyntaxKind::Symmetric) {
        if (similarity == 0.0 || removed_size == a_size) {
            return make_list2(a, b);
        }
        nb::dict result;
        if (PySet_GET_SIZE(added.ptr()) != 0) {
            dict_set(result, g_symbols.add, added);
        }
        if (removed_size != 0) {
            dict_set(result, g_symbols.discard, removed);
        }
        return result;
    }

    if (syntax == SyntaxKind::RightOnly && (similarity == 0.0 || removed_size == a_size)) {
        return nb::borrow<nb::object>(b);
    }

    if (similarity == 1.0) {
        return nb::dict();
    }
    nb::dict result;
    if (removed_size != 0) {
        dict_set(result, g_symbols.discard, removed);
    }
    if (PySet_GET_SIZE(added.ptr()) != 0) {
        dict_set(result, g_symbols.add, added);
    }
    return result;
}

static DiffResult obj_diff(
    nb::handle a,
    nb::handle b,
    SyntaxKind syntax,
    const std::unordered_set<std::string> &exclude_paths,
    const std::string &path
);

static DiffResult list_diff(nb::handle a, nb::handle b, SyntaxKind syntax) {
    std::vector<nb::object> x = sequence_to_vector(a);
    std::vector<nb::object> y = sequence_to_vector(b);
    size_t m = x.size();
    size_t n = y.size();
    std::vector<double> matrix((m + 1) * (n + 1), 0.0);
    auto at = [&](size_t i, size_t j) -> double & {
        return matrix[i * (n + 1) + j];
    };
    std::unordered_set<std::string> empty_excludes;

    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            DiffResult nested = obj_diff(x[i - 1], y[j - 1], syntax, empty_excludes, "");
            at(i, j) = std::max({at(i, j - 1), at(i - 1, j), at(i - 1, j - 1) + nested.similarity});
        }
    }

    nb::list inserted;
    nb::list deleted_positions;
    nb::list deleted_pairs;
    nb::dict changed;
    double total_similarity = 0.0;
    size_t i = m;
    size_t j = n;
    struct Step {
        int sign;
        nb::object value;
        long pos;
        double similarity;
    };
    std::vector<Step> steps;

    while (true) {
        if (i > 0 && j > 0) {
            DiffResult nested = obj_diff(x[i - 1], y[j - 1], syntax, empty_excludes, "");
            if (nested.similarity > 0.0 && at(i, j) == at(i - 1, j - 1) + nested.similarity) {
                steps.push_back(Step{0, nested.diff, static_cast<long>(j - 1), nested.similarity});
                --i;
                --j;
                continue;
            }
        }
        if (j > 0 && (i == 0 || at(i, j - 1) >= at(i - 1, j))) {
            steps.push_back(Step{1, y[j - 1], static_cast<long>(j - 1), 0.0});
            --j;
            continue;
        }
        if (i > 0 && (j == 0 || at(i, j - 1) < at(i - 1, j))) {
            steps.push_back(Step{-1, x[i - 1], static_cast<long>(i - 1), 0.0});
            --i;
            continue;
        }
        break;
    }

    for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
        if (it->sign == 1) {
            list_append(inserted, make_tuple2(nb::int_(it->pos), it->value));
        } else if (it->sign == -1) {
            if (PyList_Insert(deleted_positions.ptr(), 0, nb::int_(it->pos).ptr()) != 0) {
                throw nb::python_error();
            }
            if (PyList_Insert(deleted_pairs.ptr(), 0, make_tuple2(nb::int_(it->pos), it->value).ptr()) != 0) {
                throw nb::python_error();
            }
        } else {
            if (it->similarity < 1.0) {
                dict_set(changed, nb::int_(it->pos), it->value);
            }
        }
        total_similarity += it->similarity;
    }

    double total_count = static_cast<double>(m + static_cast<size_t>(py_len(inserted)));
    double similarity = total_count == 0.0 ? 1.0 : total_similarity / total_count;
    return DiffResult{
        emit_list_diff(syntax, a, b, similarity, inserted, changed, deleted_positions, deleted_pairs),
        similarity,
    };
}

static DiffResult set_diff(nb::handle a, nb::handle b, SyntaxKind syntax) {
    std::vector<nb::object> removed_items;
    std::vector<nb::object> added_items;

    for (const auto &item : iterable_to_vector(a)) {
        int contains = PySet_Contains(b.ptr(), item.ptr());
        if (contains < 0) {
            throw nb::python_error();
        }
        if (contains == 0) {
            removed_items.push_back(nb::borrow<nb::object>(item));
        }
    }

    for (const auto &item : iterable_to_vector(b)) {
        int contains = PySet_Contains(a.ptr(), item.ptr());
        if (contains < 0) {
            throw nb::python_error();
        }
        if (contains == 0) {
            added_items.push_back(nb::borrow<nb::object>(item));
        }
    }

    if (removed_items.empty() && added_items.empty()) {
        return DiffResult{nb::dict(), 1.0};
    }

    struct RankingEntry {
        double similarity;
        size_t removed_index;
        size_t added_index;
    };

    std::vector<RankingEntry> ranking;
    std::unordered_set<std::string> empty_excludes;

    for (size_t i = 0; i < removed_items.size(); ++i) {
        for (size_t j = 0; j < added_items.size(); ++j) {
            DiffResult nested = obj_diff(removed_items[i], added_items[j], syntax, empty_excludes, "");
            ranking.push_back(RankingEntry{nested.similarity, i, j});
        }
    }

    std::sort(ranking.begin(), ranking.end(), [](const RankingEntry &lhs, const RankingEntry &rhs) {
        return lhs.similarity > rhs.similarity;
    });

    std::vector<bool> removed_used(removed_items.size(), false);
    std::vector<bool> added_used(added_items.size(), false);
    long shared_count = static_cast<long>(PySet_GET_SIZE(a.ptr()) - static_cast<Py_ssize_t>(removed_items.size()));
    double shared_similarity = static_cast<double>(shared_count);

    for (const auto &entry : ranking) {
        if (!removed_used[entry.removed_index] && !added_used[entry.added_index]) {
            removed_used[entry.removed_index] = true;
            added_used[entry.added_index] = true;
            shared_similarity += entry.similarity;
            ++shared_count;
        }
    }

    double total = static_cast<double>(PySet_GET_SIZE(a.ptr()) + static_cast<Py_ssize_t>(added_items.size()));
    double similarity = total == 0.0 ? 1.0 : shared_similarity / total;
    nb::set removed = set_from_vector(removed_items);
    nb::set added = set_from_vector(added_items);
    return DiffResult{emit_set_diff(syntax, a, b, similarity, added, removed), similarity};
}

static DiffResult dict_diff(
    nb::handle a,
    nb::handle b,
    SyntaxKind syntax,
    const std::unordered_set<std::string> &exclude_paths,
    const std::string &path
) {
    nb::dict removed;
    nb::dict added;
    nb::dict changed;
    long removed_count = 0;
    long added_count = 0;
    long matched_count = 0;
    double matched_similarity = 0.0;

    PyObject *key = nullptr;
    PyObject *value = nullptr;
    Py_ssize_t pos = 0;

    if (exclude_paths.empty()) {
        while (PyDict_Next(a.ptr(), &pos, &key, &value)) {
            PyObject *other = PyDict_GetItemWithError(b.ptr(), key);
            if (other == nullptr) {
                if (PyErr_Occurred() != nullptr) {
                    throw nb::python_error();
                }
                ++removed_count;
                dict_set(removed, nb::handle(key), nb::handle(value));
                continue;
            }

            ++matched_count;
            DiffResult nested = obj_diff(nb::handle(value), nb::handle(other), syntax, exclude_paths, "");
            if (nested.similarity < 1.0) {
                dict_set(changed, nb::handle(key), nested.diff);
            }
            matched_similarity += 0.5 + 0.5 * nested.similarity;
        }

        pos = 0;
        while (PyDict_Next(b.ptr(), &pos, &key, &value)) {
            int contains = PyDict_Contains(a.ptr(), key);
            if (contains < 0) {
                throw nb::python_error();
            }
            if (contains == 0) {
                ++added_count;
                dict_set(added, nb::handle(key), nb::handle(value));
            }
        }
    } else {
        while (PyDict_Next(a.ptr(), &pos, &key, &value)) {
            std::string new_path = join_path(path, nb::handle(key));
            if (exclude_paths.find(new_path) != exclude_paths.end()) {
                continue;
            }

            PyObject *other = PyDict_GetItemWithError(b.ptr(), key);
            if (other == nullptr) {
                if (PyErr_Occurred() != nullptr) {
                    throw nb::python_error();
                }
                ++removed_count;
                dict_set(removed, nb::handle(key), nb::handle(value));
                continue;
            }

            ++matched_count;
            DiffResult nested = obj_diff(nb::handle(value), nb::handle(other), syntax, exclude_paths, new_path);
            if (nested.similarity < 1.0) {
                dict_set(changed, nb::handle(key), nested.diff);
            }
            matched_similarity += 0.5 + 0.5 * nested.similarity;
        }

        pos = 0;
        while (PyDict_Next(b.ptr(), &pos, &key, &value)) {
            int contains = PyDict_Contains(a.ptr(), key);
            if (contains < 0) {
                throw nb::python_error();
            }
            if (contains == 0) {
                std::string new_path = join_path(path, nb::handle(key));
                if (exclude_paths.find(new_path) != exclude_paths.end()) {
                    continue;
                }
                ++added_count;
                dict_set(added, nb::handle(key), nb::handle(value));
            }
        }
    }

    long total = removed_count + matched_count + added_count;
    double similarity = total == 0 ? 1.0 : matched_similarity / static_cast<double>(total);
    return DiffResult{emit_dict_diff(syntax, a, b, similarity, added, changed, removed), similarity};
}

static DiffResult obj_diff(
    nb::handle a,
    nb::handle b,
    SyntaxKind syntax,
    const std::unordered_set<std::string> &exclude_paths,
    const std::string &path
) {
    if (!exclude_paths.empty() && exclude_paths.find(path) != exclude_paths.end()) {
        return DiffResult{nb::dict(), 1.0};
    }
    if (a.ptr() == b.ptr()) {
        return DiffResult{emit_value_diff(syntax, a, b, 1.0), 1.0};
    }
    if (is_dict(a) && is_dict(b)) {
        return dict_diff(a, b, syntax, exclude_paths, path);
    }
    if (is_tuple(a) && is_tuple(b)) {
        return list_diff(a, b, syntax);
    }
    if (is_list(a) && is_list(b)) {
        return list_diff(a, b, syntax);
    }
    if (is_set(a) && is_set(b)) {
        return set_diff(a, b, syntax);
    }
    if (!py_equal(a, b)) {
        return DiffResult{emit_value_diff(syntax, a, b, 0.0), 0.0};
    }
    return DiffResult{emit_value_diff(syntax, a, b, 1.0), 1.0};
}

static nb::object patch_compact_like(nb::handle a, nb::handle d) {
    if (is_dict(d)) {
        if (py_len(d) == 0) {
            return nb::borrow<nb::object>(a);
        }
        if (dict_contains(d, g_symbols.replace)) {
            return dict_get(d, g_symbols.replace);
        }
        if (is_dict(a)) {
            nb::dict result = copy_dict(a);
            for (const auto &key : dict_keys(d)) {
                nb::object value = dict_get(d, key);
                if (py_equal(key, g_symbols.delete_)) {
                    dict_delete_keys(result, value);
                } else {
                    if (!dict_contains(result, key)) {
                        dict_set(result, key, value);
                    } else {
                        nb::object current = dict_get(result, key);
                        dict_set(result, key, patch_compact_like(current, value));
                    }
                }
            }
            return result;
        }
        if (is_list_like(a)) {
            bool was_tuple = is_tuple(a);
            nb::list result = copy_list(a);
            if (dict_contains(d, g_symbols.delete_)) {
                list_delete_positions(result, dict_get(d, g_symbols.delete_));
            }
            if (dict_contains(d, g_symbols.insert)) {
                list_insert_pairs(result, dict_get(d, g_symbols.insert));
            }
            patch_list_items(result, d, patch_compact_like);
            if (was_tuple) {
                return list_to_tuple(result);
            }
            return result;
        }
        if (is_set(a)) {
            nb::set result = copy_set(a);
            if (dict_contains(d, g_symbols.discard)) {
                set_discard_all(result, dict_get(d, g_symbols.discard));
            }
            if (dict_contains(d, g_symbols.add)) {
                set_add_all(result, dict_get(d, g_symbols.add));
            }
            return result;
        }
    }
    return nb::borrow<nb::object>(d);
}

static nb::object patch_symmetric(nb::handle a, nb::handle d) {
    if (is_list(d)) {
        PyObject *value = PySequence_GetItem(d.ptr(), 1);
        if (value == nullptr) {
            throw nb::python_error();
        }
        return nb::steal<nb::object>(value);
    }
    if (is_dict(d)) {
        if (py_len(d) == 0) {
            return nb::borrow<nb::object>(a);
        }
        if (is_dict(a)) {
            nb::dict result = copy_dict(a);
            for (const auto &key : dict_keys(d)) {
                nb::object value = dict_get(d, key);
                if (py_equal(key, g_symbols.delete_)) {
                    dict_delete_mapping_keys(result, value);
                } else if (py_equal(key, g_symbols.insert)) {
                    dict_copy_items(result, value);
                } else {
                    dict_set(result, key, patch_symmetric(dict_get(result, key), value));
                }
            }
            return result;
        }
        if (is_list_like(a)) {
            bool was_tuple = is_tuple(a);
            nb::list result = copy_list(a);
            if (dict_contains(d, g_symbols.delete_)) {
                list_delete_pairs(result, dict_get(d, g_symbols.delete_));
            }
            if (dict_contains(d, g_symbols.insert)) {
                list_insert_pairs(result, dict_get(d, g_symbols.insert));
            }
            patch_list_items(result, d, patch_symmetric);
            if (was_tuple) {
                return list_to_tuple(result);
            }
            return result;
        }
        if (is_set(a)) {
            nb::set result = copy_set(a);
            if (dict_contains(d, g_symbols.discard)) {
                set_discard_all(result, dict_get(d, g_symbols.discard));
            }
            if (dict_contains(d, g_symbols.add)) {
                set_add_all(result, dict_get(d, g_symbols.add));
            }
            return result;
        }
    }
    throw nb::value_error("Invalid symmetric diff");
}

static nb::object unpatch_symmetric(nb::handle b, nb::handle d) {
    if (is_list(d)) {
        PyObject *value = PySequence_GetItem(d.ptr(), 0);
        if (value == nullptr) {
            throw nb::python_error();
        }
        return nb::steal<nb::object>(value);
    }
    if (is_dict(d)) {
        if (py_len(d) == 0) {
            return nb::borrow<nb::object>(b);
        }
        if (is_dict(b)) {
            nb::dict result = copy_dict(b);
            for (const auto &key : dict_keys(d)) {
                nb::object value = dict_get(d, key);
                if (py_equal(key, g_symbols.delete_)) {
                    dict_copy_items(result, value);
                } else if (py_equal(key, g_symbols.insert)) {
                    dict_delete_mapping_keys(result, value);
                } else {
                    dict_set(result, key, unpatch_symmetric(dict_get(result, key), value));
                }
            }
            return result;
        }
        if (is_list_like(b)) {
            bool was_tuple = is_tuple(b);
            nb::list result = copy_list(b);
            patch_list_items(result, d, unpatch_symmetric);
            if (dict_contains(d, g_symbols.insert)) {
                list_delete_pairs(result, dict_get(d, g_symbols.insert), true);
            }
            if (dict_contains(d, g_symbols.delete_)) {
                list_insert_pairs(result, dict_get(d, g_symbols.delete_), true);
            }
            if (was_tuple) {
                return list_to_tuple(result);
            }
            return result;
        }
        if (is_set(b)) {
            nb::set result = copy_set(b);
            if (dict_contains(d, g_symbols.discard)) {
                set_add_all(result, dict_get(d, g_symbols.discard));
            }
            if (dict_contains(d, g_symbols.add)) {
                set_discard_all(result, dict_get(d, g_symbols.add));
            }
            return result;
        }
    }
    throw nb::value_error("Invalid symmetric diff");
}

class JsonDumper {
public:
    explicit JsonDumper(nb::dict kwargs = nb::dict()) : kwargs_(std::move(kwargs)) {}

    nb::object operator()(nb::object obj, nb::object dest = nb::none()) const {
        nb::module_ json = nb::module_::import_("json");
        if (dest.is_none()) {
            return call_python(nb::getattr(json, "dumps"), {obj}, kwargs_);
        }
        return call_python(nb::getattr(json, "dump"), {obj, dest}, kwargs_);
    }

private:
    nb::dict kwargs_;
};

class YamlDumper {
public:
    explicit YamlDumper(nb::dict kwargs = nb::dict()) : kwargs_(std::move(kwargs)) {}

    nb::object operator()(nb::object obj, nb::object dest = nb::none()) const {
        nb::module_ yaml = nb::module_::import_("yaml");
        return call_python(nb::getattr(yaml, "dump"), {obj, dest}, kwargs_);
    }

private:
    nb::dict kwargs_;
};

class JsonLoader {
public:
    explicit JsonLoader(nb::dict kwargs = nb::dict()) : kwargs_(std::move(kwargs)) {}

    nb::object operator()(nb::object src) const {
        nb::module_ json = nb::module_::import_("json");
        if (PyUnicode_Check(src.ptr())) {
            return call_python(nb::getattr(json, "loads"), {src}, kwargs_);
        }
        return call_python(nb::getattr(json, "load"), {src}, kwargs_);
    }

private:
    nb::dict kwargs_;
};

class YamlLoader {
public:
    YamlLoader() = default;

    nb::object operator()(nb::object src) const {
        nb::module_ yaml = nb::module_::import_("yaml");
        return call_python(nb::getattr(yaml, "safe_load"), {src});
    }
};

class Serializer {
public:
    Serializer(std::string file_format, nb::object indent)
        : file_format_(std::move(file_format)) {
        if (file_format_ == "json") {
            loader_ = nb::cast(JsonLoader());
            nb::dict kwargs;
            if (!indent.is_none()) {
                dict_set(kwargs, nb::str("indent"), indent);
            }
            dumper_ = nb::cast(JsonDumper(kwargs));
            return;
        }
        if (file_format_ == "yaml") {
            loader_ = nb::cast(YamlLoader());
            nb::dict kwargs;
            if (!indent.is_none()) {
                dict_set(kwargs, nb::str("indent"), indent);
            }
            dumper_ = nb::cast(YamlDumper(kwargs));
            return;
        }
        std::string message =
            "Unsupported serialization format " + file_format_ + ", expected one of dict_keys(['json', 'yaml'])";
        throw nb::value_error(message.c_str());
    }

    nb::object deserialize_file(nb::object src) const {
        try {
            return call_python(loader_, {src});
        } catch (const nb::python_error &) {
            nb::module_ json = nb::module_::import_("json");
            nb::module_ yaml = nb::module_::import_("yaml");
            if (PyErr_ExceptionMatches(nb::getattr(json, "JSONDecodeError").ptr()) ||
                PyErr_ExceptionMatches(nb::getattr(yaml, "YAMLError").ptr())) {
                PyErr_Clear();
                std::string message = "Invalid " + file_format_ + " file";
                throw nb::value_error(message.c_str());
            }
            throw;
        }
    }

    void serialize_data(nb::object obj, nb::object stream) const {
        call_python(dumper_, {obj, stream});
    }

    const std::string &file_format() const {
        return file_format_;
    }

private:
    std::string file_format_;
    nb::object loader_;
    nb::object dumper_;
};

class JsonDiffer {
public:
    JsonDiffer(
        nb::object syntax = nb::str("compact"),
        bool load = false,
        bool dump = false,
        bool marshal = false,
        nb::object loader = nb::none(),
        nb::object dumper = nb::none(),
        std::string escape_str = "$"
    )
        : syntax_kind_(syntax_from_object(syntax)),
          load_(load),
          dump_(dump),
          marshal_(marshal),
          loader_(loader.is_none() ? g_default_loader : loader),
          dumper_(dumper.is_none() ? g_default_dumper : dumper),
          escape_str_(std::move(escape_str)) {
        symbol_map_[escape_str_ + "missing"] = g_symbols.missing;
        symbol_map_[escape_str_ + "identical"] = g_symbols.identical;
        symbol_map_[escape_str_ + "delete"] = g_symbols.delete_;
        symbol_map_[escape_str_ + "insert"] = g_symbols.insert;
        symbol_map_[escape_str_ + "update"] = g_symbols.update;
        symbol_map_[escape_str_ + "add"] = g_symbols.add;
        symbol_map_[escape_str_ + "discard"] = g_symbols.discard;
        symbol_map_[escape_str_ + "replace"] = g_symbols.replace;
        symbol_map_[escape_str_ + "left"] = g_symbols.left;
        symbol_map_[escape_str_ + "right"] = g_symbols.right;
    }

    nb::object diff(nb::object a, nb::object b, nb::object fp = nb::none(), nb::object exclude_paths_obj = nb::none()) const {
        if (load_) {
            a = call_python(loader_, {a});
            b = call_python(loader_, {b});
        }
        std::unordered_set<std::string> exclude_paths;
        if (!exclude_paths_obj.is_none()) {
            for (const auto &item : iterable_to_vector(exclude_paths_obj)) {
                exclude_paths.insert(obj_to_string(item));
            }
        }
        DiffResult result = obj_diff(a, b, syntax_kind_, exclude_paths, "");
        nb::object delta = result.diff;
        if (marshal_ || dump_) {
            delta = marshal(delta);
        }
        if (dump_) {
            return call_python(dumper_, {delta, fp});
        }
        return delta;
    }

    double similarity(nb::object a, nb::object b) const {
        if (load_) {
            a = call_python(loader_, {a});
            b = call_python(loader_, {b});
        }
        std::unordered_set<std::string> exclude_paths;
        return obj_diff(a, b, syntax_kind_, exclude_paths, "").similarity;
    }

    nb::object patch(nb::object a, nb::object d, nb::object fp = nb::none()) const {
        if (load_) {
            a = call_python(loader_, {a});
            d = call_python(loader_, {d});
        }
        if (marshal_ || load_) {
            d = unmarshal(d);
        }
        nb::object patched;
        if (syntax_kind_ == SyntaxKind::Symmetric) {
            patched = patch_symmetric(a, d);
        } else if (syntax_kind_ == SyntaxKind::Compact || syntax_kind_ == SyntaxKind::RightOnly) {
            patched = patch_compact_like(a, d);
        } else {
            throw nb::type_error("Patch is not implemented for explicit syntax");
        }
        if (dump_) {
            return call_python(dumper_, {patched, fp});
        }
        return patched;
    }

    nb::object unpatch(nb::object b, nb::object d, nb::object fp = nb::none()) const {
        if (load_) {
            b = call_python(loader_, {b});
            d = call_python(loader_, {d});
        }
        if (marshal_ || load_) {
            d = unmarshal(d);
        }
        if (syntax_kind_ != SyntaxKind::Symmetric) {
            throw nb::type_error("Unpatch is only implemented for symmetric syntax");
        }
        nb::object unpatched = unpatch_symmetric(b, d);
        if (dump_) {
            return call_python(dumper_, {unpatched, fp});
        }
        return unpatched;
    }

    nb::object marshal(nb::handle d) const {
        if (is_dict(d)) {
            nb::dict result;
            for (const auto &key : dict_keys(d)) {
                dict_set(result, escape(key), marshal(dict_get(d, key)));
            }
            return result;
        }
        if (is_list(d)) {
            nb::list result;
            for (const auto &item : iterable_to_vector(d)) {
                list_append(result, marshal(item));
            }
            return result;
        }
        if (is_tuple(d)) {
            std::vector<nb::object> items = sequence_to_vector(d);
            PyObject *tuple = PyTuple_New(static_cast<Py_ssize_t>(items.size()));
            if (tuple == nullptr) {
                throw nb::python_error();
            }
            for (size_t i = 0; i < items.size(); ++i) {
                nb::object value = marshal(items[i]);
                Py_INCREF(value.ptr());
                PyTuple_SET_ITEM(tuple, static_cast<Py_ssize_t>(i), value.ptr());
            }
            return nb::steal<nb::tuple>(tuple);
        }
        return escape(d);
    }

    nb::object unmarshal(nb::handle d) const {
        if (is_dict(d)) {
            nb::dict result;
            for (const auto &key : dict_keys(d)) {
                dict_set(result, unescape(key), unmarshal(dict_get(d, key)));
            }
            return result;
        }
        if (is_list(d)) {
            nb::list result;
            for (const auto &item : iterable_to_vector(d)) {
                list_append(result, unmarshal(item));
            }
            return result;
        }
        if (is_tuple(d)) {
            std::vector<nb::object> items = sequence_to_vector(d);
            PyObject *tuple = PyTuple_New(static_cast<Py_ssize_t>(items.size()));
            if (tuple == nullptr) {
                throw nb::python_error();
            }
            for (size_t i = 0; i < items.size(); ++i) {
                nb::object value = unmarshal(items[i]);
                Py_INCREF(value.ptr());
                PyTuple_SET_ITEM(tuple, static_cast<Py_ssize_t>(i), value.ptr());
            }
            return nb::steal<nb::tuple>(tuple);
        }
        return unescape(d);
    }

private:
    nb::object escape(nb::handle obj) const {
        if (nb::isinstance<Symbol>(obj)) {
            Symbol symbol = nb::cast<Symbol>(obj);
            std::string value = escape_str_ + symbol.label;
            return nb::str(value.c_str());
        }
        if (PyUnicode_Check(obj.ptr())) {
            std::string value = nb::cast<std::string>(nb::str(obj));
            if (value.rfind(escape_str_, 0) == 0) {
                std::string escaped = escape_str_ + value;
                return nb::str(escaped.c_str());
            }
        }
        return nb::borrow<nb::object>(obj);
    }

    nb::object unescape(nb::handle obj) const {
        if (PyUnicode_Check(obj.ptr())) {
            std::string value = nb::cast<std::string>(nb::str(obj));
            auto it = symbol_map_.find(value);
            if (it != symbol_map_.end()) {
                return it->second;
            }
            if (value.rfind(escape_str_, 0) == 0) {
                std::string unescaped = value.substr(1);
                return nb::str(unescaped.c_str());
            }
        }
        return nb::borrow<nb::object>(obj);
    }

    SyntaxKind syntax_kind_;
    bool load_;
    bool dump_;
    bool marshal_;
    nb::object loader_;
    nb::object dumper_;
    std::string escape_str_;
    std::unordered_map<std::string, nb::object> symbol_map_;
};

NB_MODULE(module, m) {
    nb::set_leak_warnings(false);
    m.attr("__version__") = "0.1.0";

    nb::class_<Symbol> symbol_cls(m, "Symbol");
    symbol_cls
        .def(nb::init<std::string>())
        .def_prop_ro("label", [](const Symbol &self) { return self.label; })
        .def("__repr__", [](const Symbol &self) { return self.label; })
        .def("__str__", [](const Symbol &self) { return "$" + self.label; })
        .def("__hash__", [](const Symbol &self) { return static_cast<Py_hash_t>(std::hash<std::string>{}(self.label)); })
        .def("__eq__", [](const Symbol &self, nb::handle other) {
            if (!nb::isinstance<Symbol>(other)) {
                return false;
            }
            return self.label == nb::cast<Symbol>(other).label;
        });

    auto set_symbol = [&](const char *name) -> nb::object {
        nb::object obj = nb::cast(Symbol{std::string(name)});
        m.attr(name) = obj;
        return obj;
    };

    g_symbols.missing = set_symbol("missing");
    g_symbols.identical = set_symbol("identical");
    g_symbols.delete_ = set_symbol("delete");
    g_symbols.insert = set_symbol("insert");
    g_symbols.update = set_symbol("update");
    g_symbols.add = set_symbol("add");
    g_symbols.discard = set_symbol("discard");
    g_symbols.replace = set_symbol("replace");
    g_symbols.left = set_symbol("left");
    g_symbols.right = set_symbol("right");

    nb::class_<JsonDumper> json_dumper_cls(m, "JsonDumper");
    json_dumper_cls
        .def("__init__", [](JsonDumper *self, nb::kwargs kwargs) {
            new (self) JsonDumper(nb::dict(kwargs));
        })
        .def(
            "__call__",
            [](JsonDumper &self, nb::handle obj, nb::object dest) {
                return self(nb::borrow<nb::object>(obj), dest);
            },
            nb::arg("obj").none(),
            nb::arg("dest") = nb::none()
        );

    nb::class_<YamlDumper> yaml_dumper_cls(m, "YamlDumper");
    yaml_dumper_cls
        .def("__init__", [](YamlDumper *self, nb::kwargs kwargs) {
            new (self) YamlDumper(nb::dict(kwargs));
        })
        .def(
            "__call__",
            [](YamlDumper &self, nb::handle obj, nb::object dest) {
                return self(nb::borrow<nb::object>(obj), dest);
            },
            nb::arg("obj").none(),
            nb::arg("dest") = nb::none()
        );

    nb::class_<JsonLoader> json_loader_cls(m, "JsonLoader");
    json_loader_cls
        .def("__init__", [](JsonLoader *self, nb::kwargs kwargs) {
            new (self) JsonLoader(nb::dict(kwargs));
        })
        .def("__call__", &JsonLoader::operator(), nb::arg("src"));

    nb::class_<YamlLoader>(m, "YamlLoader")
        .def(nb::init<>())
        .def("__call__", &YamlLoader::operator(), nb::arg("src"));

    nb::class_<Serializer>(m, "Serializer")
        .def(nb::init<std::string, nb::object>(), nb::arg("file_format"), nb::arg("indent") = nb::none())
        .def("deserialize_file", &Serializer::deserialize_file, nb::arg("src"))
        .def("serialize_data", &Serializer::serialize_data, nb::arg("obj"), nb::arg("stream"))
        .def_prop_ro("file_format", &Serializer::file_format);

    nb::class_<CompactJsonDiffSyntax>(m, "CompactJsonDiffSyntax").def(nb::init<>());
    nb::class_<ExplicitJsonDiffSyntax>(m, "ExplicitJsonDiffSyntax").def(nb::init<>());
    nb::class_<SymmetricJsonDiffSyntax>(m, "SymmetricJsonDiffSyntax").def(nb::init<>());
    nb::class_<RightOnlyJsonDiffSyntax>(m, "RightOnlyJsonDiffSyntax").def(nb::init<>());

    g_default_dumper = nb::cast(JsonDumper());
    g_default_loader = nb::cast(JsonLoader());
    m.attr("default_dumper") = g_default_dumper;
    m.attr("default_loader") = g_default_loader;

    nb::class_<JsonDiffer>(m, "JsonDiffer")
        .def(
            nb::init<nb::object, bool, bool, bool, nb::object, nb::object, std::string>(),
            nb::arg("syntax") = nb::str("compact"),
            nb::arg("load") = false,
            nb::arg("dump") = false,
            nb::arg("marshal") = false,
            nb::arg("loader") = nb::none(),
            nb::arg("dumper") = nb::none(),
            nb::arg("escape_str") = "$"
        )
        .def("diff", &JsonDiffer::diff, nb::arg("a").none(), nb::arg("b").none(), nb::arg("fp") = nb::none(), nb::arg("exclude_paths") = nb::none())
        .def("similarity", &JsonDiffer::similarity, nb::arg("a").none(), nb::arg("b").none())
        .def("patch", &JsonDiffer::patch, nb::arg("a").none(), nb::arg("d").none(), nb::arg("fp") = nb::none())
        .def("unpatch", &JsonDiffer::unpatch, nb::arg("b").none(), nb::arg("d").none(), nb::arg("fp") = nb::none())
        .def(
            "marshal",
            [](JsonDiffer &self, nb::handle d) {
                return self.marshal(d);
            },
            nb::arg("d").none()
        )
        .def(
            "unmarshal",
            [](JsonDiffer &self, nb::handle d) {
                return self.unmarshal(d);
            },
            nb::arg("d").none()
        );

    m.def(
        "diff",
        [](nb::object a, nb::object b, nb::object fp, nb::object syntax, bool load, bool dump, bool marshal, nb::object loader, nb::object dumper, std::string escape_str, nb::object exclude_paths) {
            JsonDiffer differ(syntax, load, dump, marshal, loader, dumper, escape_str);
            return differ.diff(a, b, fp, exclude_paths);
        },
        nb::arg("a").none(),
        nb::arg("b").none(),
        nb::arg("fp") = nb::none(),
        nb::arg("syntax") = nb::str("compact"),
        nb::arg("load") = false,
        nb::arg("dump") = false,
        nb::arg("marshal") = false,
        nb::arg("loader") = nb::none(),
        nb::arg("dumper") = nb::none(),
        nb::arg("escape_str") = "$",
        nb::arg("exclude_paths") = nb::none()
    );

    m.def(
        "patch",
        [](nb::object a, nb::object d, nb::object fp, nb::object syntax, bool load, bool dump, bool marshal, nb::object loader, nb::object dumper, std::string escape_str) {
            JsonDiffer differ(syntax, load, dump, marshal, loader, dumper, escape_str);
            return differ.patch(a, d, fp);
        },
        nb::arg("a").none(),
        nb::arg("d").none(),
        nb::arg("fp") = nb::none(),
        nb::arg("syntax") = nb::str("compact"),
        nb::arg("load") = false,
        nb::arg("dump") = false,
        nb::arg("marshal") = false,
        nb::arg("loader") = nb::none(),
        nb::arg("dumper") = nb::none(),
        nb::arg("escape_str") = "$"
    );

    m.def(
        "unpatch",
        [](nb::object b, nb::object d, nb::object fp, nb::object syntax, bool load, bool dump, bool marshal, nb::object loader, nb::object dumper, std::string escape_str) {
            JsonDiffer differ(syntax, load, dump, marshal, loader, dumper, escape_str);
            return differ.unpatch(b, d, fp);
        },
        nb::arg("b").none(),
        nb::arg("d").none(),
        nb::arg("fp") = nb::none(),
        nb::arg("syntax") = nb::str("compact"),
        nb::arg("load") = false,
        nb::arg("dump") = false,
        nb::arg("marshal") = false,
        nb::arg("loader") = nb::none(),
        nb::arg("dumper") = nb::none(),
        nb::arg("escape_str") = "$"
    );

    m.def(
        "similarity",
        [](nb::object a, nb::object b, nb::object syntax, bool load, nb::object loader, std::string escape_str) {
            JsonDiffer differ(syntax, load, false, false, loader, nb::none(), escape_str);
            return differ.similarity(a, b);
        },
        nb::arg("a").none(),
        nb::arg("b").none(),
        nb::arg("syntax") = nb::str("compact"),
        nb::arg("load") = false,
        nb::arg("loader") = nb::none(),
        nb::arg("escape_str") = "$"
    );
}
