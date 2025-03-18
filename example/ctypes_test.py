import ctypes

class A(ctypes.Structure):
	_fields_ = [("value", ctypes.c_int)]

a1 = A()
a2 = ctypes.pointer(a1).contents
a3 = ctypes.cast(ctypes.byref(a1), ctypes.POINTER(A)).contents

print(f"a1 == a2 : {a1 == a2} {type(a1)} {type(a2)}")
print(f"a1 == a3 : {a1 == a3} {type(a1)} {type(a3)}")
print(f"a2 == a3 : {a2 == a3} {type(a2)} {type(a3)}")

a4 = ctypes.pointer(a2).contents
print(f"a2 == a4 : {a2 == a4} {type(a2)} {type(a4)}")

a1.value = 1
print(f"value : {a1.value}")
a2.value = 2
print(f"value : {a1.value}")
a3.value = 3
print(f"value : {a1.value}")