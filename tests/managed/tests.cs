/*
 * Unified managed fixture for the dotnetpp test suite. The same file is
 * compiled and used on both backends:
 *   - mono resolves [MethodImpl(InternalCall)] extern methods natively.
 *   - coreclr rewrites them at load time through the IL weaver (see
 *     clrpp/managed/Weaver.cs), so no Clrpp.* references are needed here.
 */
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Tests
{

namespace Nested
{
class TestClassNested1
{
	class TestClassNested2
	{
		public int someField = 12;
	}
	public int someField = 12;
}
}

public enum TestEnum
{
	First = 1,
	Second = 5,
	Third = 42
}

class MyObject
{
	public MyObject()
	{
		CreateInternal(5.0f, "test");
	}

	~MyObject()
	{
		DestroyInternal();
	}

	[MethodImpl(MethodImplOptions.InternalCall)]
	private extern void CreateInternal(float x, string s);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private extern void DestroyInternal();

	[MethodImpl(MethodImplOptions.InternalCall)]
	public extern void DoStuff(string value);

	[MethodImpl(MethodImplOptions.InternalCall)]
	public extern string ReturnAString(string value);
}

class MonoppTest
{
	public int someField = 12;
	public string someFieldStr = "InitialString";

	public int someProperty
	{
		get
		{
			return someField;
		}
		set
		{
			someField = value;
		}
	}

	public string somePropertyStr
	{
		get
		{
			return someFieldStr;
		}
		set
		{
			someFieldStr = value;
		}
	}

	public static int someFieldStatic = 12;
	public static string someFieldStrStatic = "InitialStatic";

	public static int somePropertyStatic
	{
		get
		{
			return someFieldStatic;
		}
		set
		{
			someFieldStatic = value;
		}
	}

	void Method1()
	{
	}

	void Method2(string s)
	{
	}

	void Method3(int s)
	{
	}

	void Method4(int s, int s1)
	{
	}

	public string Method5(string s, int b)
	{
		return "Return Value: " + s;
	}

	public static int Function1(int a)
	{
		return a + 1337;
	}

	public static void Function2(float a, int b, float c)
	{
	}

	public static void Function3(string a)
	{
	}

	public static string Function4(string str)
	{
		return "The string value was: " + str;
	}

	public static void Function5()
	{
		throw new Exception("Hello!");
	}

	public static void Function6()
	{
		Tests.MyObject obj = new Tests.MyObject();
		obj.DoStuff("blalba");
		string str = obj.ReturnAString("fafafa");
		str += "";
	}

	// Primitive marshalling matrix used by the native suite.
	public static bool EchoBool(bool v)
	{
		return !v;
	}

	public static long EchoLong(long v)
	{
		return v + 1;
	}

	public static double EchoDouble(double v)
	{
		return v * 2.0;
	}

	public static float EchoFloat(float v)
	{
		return v + 0.5f;
	}

	public static double Sum(int a, float b, double c, long d)
	{
		return a + b + c + d;
	}

	// Array / list marshalling.
	public static int SumArray(int[] values)
	{
		int sum = 0;
		foreach (int v in values)
		{
			sum += v;
		}
		return sum;
	}

	public static List<int> MakeList()
	{
		return new List<int> { 1, 2, 3 };
	}

	public static int SumList(List<int> values)
	{
		int sum = 0;
		foreach (int v in values)
		{
			sum += v;
		}
		return sum;
	}

	// Native exceptions propagating into managed code.
	[MethodImpl(MethodImplOptions.InternalCall)]
	public static extern void ThrowNative();

	public static bool CatchNativeException()
	{
		try
		{
			ThrowNative();
			return false;
		}
		catch (InvalidOperationException e)
		{
			return e.Message == "native says no";
		}
	}

	// Static internal call verified from the managed side.
	[MethodImpl(MethodImplOptions.InternalCall)]
	public static extern int NativeAdd(int a, int b);

	public static bool CheckNativeAdd()
	{
		return NativeAdd(2, 3) == 5;
	}
}

class DerivedTest : MonoppTest
{
}

public struct Vector2f
{
	public Vector2f(float _x, float _y)
	{
		x = _x;
		y = _y;
	}
	public float x;
	public float y;
}

class MonortTest
{
	[MethodImpl(MethodImplOptions.InternalCall)]
	public extern void TestInternalPODCall(Vector2f rhs);

	public Vector2f someFieldPOD = new Vector2f(12, 13);

	public Vector2f somePropertyPOD
	{
		get
		{
			return someFieldPOD;
		}
		set
		{
			someFieldPOD = value;
		}
	}

	public static Vector2f someFieldPODStatic = new Vector2f(12, 13);

	public static Vector2f somePropertyPODStatic
	{
		get
		{
			return someFieldPODStatic;
		}
		set
		{
			someFieldPODStatic = value;
		}
	}

	public Vector2f MethodPodAR(Vector2f bb)
	{
		var s = new Vector2f();
		s.x = 165.0f;
		s.y = 7.0f;
		return s;
	}
}

} // namespace Tests
