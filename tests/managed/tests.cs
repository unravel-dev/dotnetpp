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

[AttributeUsage(AttributeTargets.All, AllowMultiple = false)]
public class MarkerAttribute : Attribute
{
	public string Tag { get; private set; }

	public MarkerAttribute(string tag)
	{
		Tag = tag;
	}
}

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

public enum ByteEnum : byte
{
	Alpha = 1,
	Beta = 2
}

public interface IMarker
{
	void Ping();
}

public abstract class AbstractBase
{
	public abstract int GetValue();
}

[Serializable]
public sealed class SealedMarker
{
	public int Value = 1;
}

// Native layout mirror for converter registration tests (four floats).
public struct ColorF
{
	public float R;
	public float G;
	public float B;
	public float A;
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

[Marker("MonoppTest")]
class MonoppTest
{
	public int someField = 12;
	public string someFieldStr = "InitialString";
	public object refField;
	public MonoppTest objField;

	public const int ConstValue = 42;
	public readonly int ReadonlyValue = 7;

	[Marker("Field")]
	public int markedField = 3;

	private int[] indexedItems = new int[] { 10, 20, 30, 40 };

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

	[Marker("Property")]
	public int MarkedProperty
	{
		get { return markedField; }
		set { markedField = value; }
	}

	// Real auto-property so the suite can assert is_backing_field().
	public int AutoProperty { get; set; }

	// Default indexer name is "Item".
	public int this[int index]
	{
		get { return indexedItems[index]; }
		set { indexedItems[index] = value; }
	}

	public int ReadonlyProperty
	{
		get { return ReadonlyValue; }
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

	[Marker("Method")]
	public virtual int VirtualEcho(int value)
	{
		return value + 1;
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

	public static byte EchoByte(byte v)
	{
		return (byte)(v + 1);
	}

	public static sbyte EchoSByte(sbyte v)
	{
		return (sbyte)(v - 1);
	}

	public static ushort EchoUShort(ushort v)
	{
		return (ushort)(v + 1);
	}

	public static uint EchoUInt(uint v)
	{
		return v + 1;
	}

	public static char EchoChar(char v)
	{
		return (char)(v + 1);
	}

	public static TestEnum EchoEnum(TestEnum value)
	{
		return value;
	}

	public static int EnumToInt(TestEnum value)
	{
		return (int)value;
	}

	public static ColorF Brighten(ColorF c)
	{
		return new ColorF
		{
			R = c.R + 0.1f,
			G = c.G + 0.1f,
			B = c.B + 0.1f,
			A = c.A
		};
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

	public static int[] MakeIntArray()
	{
		return new int[] { 1, 2, 3, 4 };
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

	public static List<int> DoubleList(List<int> values)
	{
		var result = new List<int>(values.Count);
		foreach (int v in values)
		{
			result.Add(v * 2);
		}
		return result;
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
	public override int VirtualEcho(int value)
	{
		return value + 10;
	}

	public int derivedOnlyField = 99;
}

class ObjectArrayHost
{
	public static MonoppTest[] MakeObjectArray()
	{
		return new MonoppTest[] { new MonoppTest(), new MonoppTest() };
	}

	public static int CountValid(MonoppTest[] values)
	{
		int count = 0;
		foreach (var v in values)
		{
			if (v != null)
			{
				count++;
			}
		}
		return count;
	}
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

// Sub-word field layout probe: bool is 1 byte and char is 2-byte utf16 in
// the CLR layout (which matches the C++ struct the native side memcpys).
// Interop-style marshalling (4-byte BOOL, ANSI char) would corrupt every
// field after `Before` - these tests exist to catch that regression.
// Layout: Before@0, Value@4, After@8, Letter@10, size 12.
public struct BoolPack
{
	public bool Before;
	public float Value;
	public bool After;
	public char Letter;
}

// Smaller than its interop-marshalled size (2 bytes vs 8) - round-trips
// only when the CLR layout is used on both sides.
public struct TwoBools
{
	public bool A;
	public bool B;
}

// Mirrors the engine's Entity: a managed struct wrapping a single scalar
// that the native side receives as a plain integer (entt::entity). Passing
// it must be indistinguishable from passing the scalar itself, which only
// holds under the by-value struct ABI - a pointer-passing convention hands
// native an address where it expects the id.
public struct WrappedId
{
	public uint Id;
}

class PackTest
{
	public static BoolPack packField =
		new BoolPack { Before = true, Value = 2.5f, After = false, Letter = 'x' };

	public static TwoBools flagsField = new TwoBools { A = true, B = false };

	public static BoolPack MakePack(bool before, float value, bool after, char letter)
	{
		return new BoolPack { Before = before, Value = value, After = after, Letter = letter };
	}

	// Verifies against fixed values so the native caller only needs the
	// struct argument (mixed known/unknown signature lookups are avoided).
	public static bool CheckPack(BoolPack p)
	{
		return p.Before == true && p.Value == 3.5f && p.After == false && p.Letter == 'k';
	}

	public static BoolPack InvertPack(BoolPack p)
	{
		return new BoolPack
		{
			Before = !p.Before,
			Value = -p.Value,
			After = !p.After,
			Letter = (char)(p.Letter + 1)
		};
	}

	// Bool-bearing structs through icalls, by value and by ref.
	[MethodImpl(MethodImplOptions.InternalCall)]
	public static extern BoolPack NativeInvertPack(BoolPack pack);

	[MethodImpl(MethodImplOptions.InternalCall)]
	public static extern void NativeFillPack(ref BoolPack pack);

	public static bool CheckNativeInvertPack()
	{
		var inverted = NativeInvertPack(MakePack(true, 2.0f, false, 'a'));
		return inverted.Before == false && inverted.Value == -2.0f &&
			   inverted.After == true && inverted.Letter == 'b';
	}

	public static bool CheckNativeFillPack()
	{
		var pack = default(BoolPack);
		NativeFillPack(ref pack);
		return pack.Before == true && pack.Value == 7.0f &&
			   pack.After == false && pack.Letter == 'q';
	}

	// Scalar-wrapper struct (see WrappedId): native registers a function
	// taking/returning a plain uint32_t, exactly like the engine's Entity
	// icalls take entt::entity.
	[MethodImpl(MethodImplOptions.InternalCall)]
	public static extern WrappedId NativeBumpId(WrappedId id);

	public static bool CheckNativeBumpId()
	{
		var bumped = NativeBumpId(new WrappedId { Id = 41 });
		return bumped.Id == 42;
	}

	// Standalone chars widen to int32 on the wire (the default interop
	// treatment of char in an unmanaged signature is 1-byte ANSI, which
	// would truncate utf16 values).
	[MethodImpl(MethodImplOptions.InternalCall)]
	public static extern char NativeNextChar(char value);

	public static bool CheckNativeNextChar()
	{
		return NativeNextChar('a') == 'b' && NativeNextChar('\u03B1') == '\u03B2';
	}

	public struct NotBlittable
	{
		public string Name;
	}

	// Weaver must skip this one (reference field in a by-value struct)
	// without affecting the other externs in this class.
	[MethodImpl(MethodImplOptions.InternalCall)]
	public static extern void RejectNonBlittable(NotBlittable value);

	public static bool RejectNonBlittableIsUncallable()
	{
		try
		{
			RejectNonBlittable(new NotBlittable { Name = "x" });
			return false;
		}
		catch
		{
			return true;
		}
	}
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
