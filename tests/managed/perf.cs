/*
 * Managed fixture for the dotnetpp performance harness (dotnetpp_perf.cpp).
 * Compiled and run unchanged on both backends, like tests.cs:
 *   - mono resolves [MethodImpl(InternalCall)] externs natively.
 *   - coreclr weaves them during compilation (clrpp/managed/Weaver.cs).
 *
 * Two kinds of workloads live here:
 *   - Managed-only loops that measure raw runtime execution speed
 *     (JIT/interpreter quality, allocation and GC throughput).
 *   - Icall loops that call into native code once per iteration so the
 *     managed -> native crossing cost dominates the measurement.
 *
 * Every loop method takes the iteration count from the native side and
 * returns a value derived from the work so the runtime cannot eliminate
 * the loop as dead code.
 */
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Perf
{

public struct Vec2
{
	public float x;
	public float y;
}

/*
 * Custom type exercised through the converter layer: the native side keeps
 * a packed byte color (4 bytes) while this managed representation is four
 * floats (16 bytes), so every crossing runs a real conversion - the same
 * pattern the engine uses for math::color and friends.
 */
public struct Color
{
	public float r;
	public float g;
	public float b;
	public float a;
}

public class PerfTest
{
	// Static storage exercised from native field benchmarks (converted type
	// and reference-type array handle).
	public static Color colorField = new Color { r = 0.25f, g = 0.5f, b = 0.75f, a = 1.0f };
	public static int[] intArrayField = new int[] { 1, 2, 3, 4 };

	// ---------------------------------------------------------------------
	// Native -> managed call targets. The native harness invokes these once
	// per iteration to measure the reflection-invoke crossing cost.
	// ---------------------------------------------------------------------

	public int counter = 0;

	public int counterProperty
	{
		get
		{
			return counter;
		}
		set
		{
			counter = value;
		}
	}

	public static int Add(int a, int b)
	{
		return a + b;
	}

	/// <summary>
	/// Four-arg blittable static — Portable CreateDelegate covers arity ≤ 8;
	/// Compiled DynamicMethod covers this when IsDynamicCodeCompiled.
	/// </summary>
	public static int Add4(int a, int b, int c, int d)
	{
		return a + b + c + d;
	}

	public int AddInstance(int a, int b)
	{
		return a + b + counter;
	}

	public static string EchoString(string s)
	{
		return s;
	}

	public static Vec2 EchoVec(Vec2 v)
	{
		return v;
	}

	public static Color Brighten(Color c)
	{
		Color result;
		result.r = c.r * 0.5f + 0.1f;
		result.g = c.g * 0.5f + 0.1f;
		result.b = c.b * 0.5f + 0.1f;
		result.a = c.a;
		return result;
	}

	// ---------------------------------------------------------------------
	// Array / list targets invoked from native code.
	// ---------------------------------------------------------------------

	public static int SumArray(int[] values)
	{
		int acc = 0;
		for(int i = 0; i < values.Length; ++i)
		{
			acc += values[i];
		}
		return acc;
	}

	public static int[] MakeArray(int n)
	{
		var result = new int[n];
		for(int i = 0; i < n; ++i)
		{
			result[i] = i;
		}
		return result;
	}

	public static float SumVecArray(Vec2[] values)
	{
		float acc = 0.0f;
		for(int i = 0; i < values.Length; ++i)
		{
			acc += values[i].x + values[i].y;
		}
		return acc;
	}

	public static int SumList(List<int> values)
	{
		int acc = 0;
		for(int i = 0; i < values.Count; ++i)
		{
			acc += values[i];
		}
		return acc;
	}

	public static List<int> MakeList(int n)
	{
		var result = new List<int>(n);
		for(int i = 0; i < n; ++i)
		{
			result.Add(i);
		}
		return result;
	}

	public static float SumColorArray(Color[] values)
	{
		float acc = 0.0f;
		for(int i = 0; i < values.Length; ++i)
		{
			acc += values[i].r + values[i].g + values[i].b + values[i].a;
		}
		return acc;
	}

	public static int BrightenColors(Color[] values)
	{
		for(int i = 0; i < values.Length; ++i)
		{
			values[i] = Brighten(values[i]);
		}
		return values.Length;
	}

	public static float SumVecList(List<Vec2> values)
	{
		float acc = 0.0f;
		for(int i = 0; i < values.Count; ++i)
		{
			acc += values[i].x + values[i].y;
		}
		return acc;
	}

	public static List<Vec2> MakeVecList(int n)
	{
		var result = new List<Vec2>(n);
		for(int i = 0; i < n; ++i)
		{
			Vec2 v;
			v.x = i;
			v.y = i * 2.0f;
			result.Add(v);
		}
		return result;
	}

	// ---------------------------------------------------------------------
	// Managed-only workloads (one native call, n iterations inside).
	// ---------------------------------------------------------------------

	public static long SumLoop(int n)
	{
		long acc = 0;
		for(int i = 0; i < n; ++i)
		{
			acc += i % 7;
		}
		return acc;
	}

	public static float StructMathLoop(int n)
	{
		Vec2 v;
		v.x = 1.0f;
		v.y = 2.0f;
		float acc = 0.0f;
		for(int i = 0; i < n; ++i)
		{
			v.x = v.x * 1.0000001f + 0.5f;
			v.y = v.y * 0.9999999f - 0.25f;
			acc += v.x * v.y;
		}
		return acc;
	}

	public static int StringLoop(int n)
	{
		int acc = 0;
		for(int i = 0; i < n; ++i)
		{
			string s = "iteration " + i.ToString();
			acc += s.Length;
		}
		return acc;
	}

	public static int AllocLoop(int n)
	{
		// Small short-lived allocations; measures allocator + nursery GC.
		int acc = 0;
		for(int i = 0; i < n; ++i)
		{
			var box = new int[4];
			box[0] = i;
			acc += box[0];
		}
		return acc;
	}

	public static int DictionaryLoop(int n)
	{
		var dict = new Dictionary<int, int>();
		int acc = 0;
		for(int i = 0; i < n; ++i)
		{
			int key = i & 1023;
			dict[key] = i;
			acc += dict[key];
		}
		return acc;
	}

	public static long ArrayFillLoop(int n)
	{
		// Managed array indexing (bounds checks, no interop involved).
		var buffer = new int[1024];
		long acc = 0;
		for(int i = 0; i < n; ++i)
		{
			buffer[i & 1023] = i;
			acc += buffer[(i + 512) & 1023];
		}
		return acc;
	}

	public static int ListChurnLoop(int n)
	{
		// List<int> add/index/clear cycle; measures growth + indexer cost.
		var list = new List<int>();
		int acc = 0;
		for(int i = 0; i < n; ++i)
		{
			list.Add(i);
			acc += list[list.Count - 1];
			if(list.Count >= 1024)
			{
				list.Clear();
			}
		}
		return acc;
	}

	public static long Vec2ArrayLoop(int n)
	{
		var buffer = new Vec2[1024];
		long acc = 0;
		for(int i = 0; i < n; ++i)
		{
			int index = i & 1023;
			buffer[index].x = i;
			buffer[index].y = i * 2;
			acc += (long)(buffer[(index + 512) & 1023].x + buffer[(index + 512) & 1023].y);
		}
		return acc;
	}

	public static int Vec2ListLoop(int n)
	{
		var list = new List<Vec2>();
		int acc = 0;
		for(int i = 0; i < n; ++i)
		{
			Vec2 v;
			v.x = i;
			v.y = i * 2.0f;
			list.Add(v);
			acc += (int)(list[list.Count - 1].x + list[list.Count - 1].y);
			if(list.Count >= 1024)
			{
				list.Clear();
			}
		}
		return acc;
	}

	public static float ColorMathLoop(int n)
	{
		Color c = colorField;
		float acc = 0.0f;
		for(int i = 0; i < n; ++i)
		{
			c = Brighten(c);
			acc += c.r + c.g + c.b;
		}
		return acc;
	}

	// ---------------------------------------------------------------------
	// Managed -> native icalls plus loops that hammer them (one native call
	// starts the loop, then every iteration crosses into native code).
	// ---------------------------------------------------------------------

	[MethodImpl(MethodImplOptions.InternalCall)]
	public static extern int NativeAdd(int a, int b);

	[MethodImpl(MethodImplOptions.InternalCall)]
	public static extern float NativeVecLenSq(Vec2 v);

	[MethodImpl(MethodImplOptions.InternalCall)]
	public static extern string NativeEcho(string s);

	[MethodImpl(MethodImplOptions.InternalCall)]
	public static extern Color NativeBrighten(Color c);

	public static long IcallAddLoop(int n)
	{
		long acc = 0;
		for(int i = 0; i < n; ++i)
		{
			acc += NativeAdd(i, 1);
		}
		return acc;
	}

	public static float IcallStructLoop(int n)
	{
		Vec2 v;
		v.x = 3.0f;
		v.y = 4.0f;
		float acc = 0.0f;
		for(int i = 0; i < n; ++i)
		{
			acc += NativeVecLenSq(v);
		}
		return acc;
	}

	public static int IcallStringLoop(int n)
	{
		int acc = 0;
		for(int i = 0; i < n; ++i)
		{
			acc += NativeEcho("perf string payload").Length;
		}
		return acc;
	}

	public static float IcallColorLoop(int n)
	{
		// Converted custom type through an icall: both directions run the
		// float <-> packed byte conversion registered on the native side.
		Color c;
		c.r = 0.1f;
		c.g = 0.2f;
		c.b = 0.3f;
		c.a = 1.0f;
		float acc = 0.0f;
		for(int i = 0; i < n; ++i)
		{
			c = NativeBrighten(c);
			acc += c.r;
		}
		return acc;
	}
}

} // namespace Perf
