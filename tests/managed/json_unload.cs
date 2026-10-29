/*
 * Fixture for the coreclr domain unload test: code in a collectible context
 * that serializes one of its own types with System.Text.Json, through a
 * static options instance and through the default options.
 */
using System.Text.Json;

namespace Tests
{

public sealed class JsonSaveData
{
	public int Coins { get; set; }
	public string Name { get; set; }
}

public static class JsonSaver
{
	private static readonly JsonSerializerOptions Options = new JsonSerializerOptions { WriteIndented = true };

	public static bool RoundTrip()
	{
		var data = new JsonSaveData { Coins = 7, Name = "unload" };
		var with_options = JsonSerializer.Deserialize<JsonSaveData>(JsonSerializer.Serialize(data, Options), Options);
		var with_defaults = JsonSerializer.Deserialize<JsonSaveData>(JsonSerializer.Serialize(data));
		return with_options.Coins == data.Coins && with_defaults.Name == data.Name;
	}
}

} // namespace Tests
