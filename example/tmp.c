namespace pybind11 {
	namespace detail {
		template <> struct type_caster<vector<char>> {
		public:
			PYBIND11_TYPE_CASTER(vector<char>, _("vector<char>"));

			bool load(handle src, bool) {
				PyObject *source = src.ptr();
				if (!PyBytes_Check(source)) return false;
				char *buffer;
				Py_ssize_t length;
				if (PyBytes_AsStringAndSize(source, &buffer, &length) == -1) return false;
				value.assign(buffer, buffer + length);
				return true;
			}

			static handle cast(const vector<char> &src, return_value_policy, handle) {
				return PyBytes_FromStringAndSize(src.data(), src.size());
			}
		};
	}
}

class SemaphoreCondition {
public:
    SemaphoreCondition(int count = 0) : count(count) {}

    void release() {
        unique_lock<mutex> lock(mtx);
        ++count;
        cv.notify_one();
    }

    void acquire() {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this]{ return count > 0; });
        --count;
    }

private:
    mutex mtx;
    condition_variable cv;
    int count;
};
