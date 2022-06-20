﻿// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using CSVStats;

namespace CSVInfo
{
    class Version
    {
        private static string VersionString = "1.05";

        public static string Get() { return VersionString; }
    };

    class Program : CommandLineTool
    {
        static int Main(string[] args)
        {
            Program program = new Program();
            if (Debugger.IsAttached)
            {
                program.Run(args);
            }
            else
            {
                try
                {
                    program.Run(args);
                }
                catch (System.Exception e)
                {
                    Console.WriteLine("[ERROR] " + e.Message);
                    return 1;
                }
            }

            return 0;
        }

		string Quotify(string s)
		{
			return "\"" + s + "\"";
		}

		string Sanitize(string s)
		{
			return s.Replace("\\","\\\\").Replace("\"", "\\\"");
		}

		string ToJsonString(string s)
		{
			return Quotify(Sanitize(s));
		}

		string ToJsonStringList(List<string> list)
		{
			List<string> safeList = new List<string>();
			foreach(string s in list)
			{
				safeList.Add(ToJsonString(s));
			}
			return "[" + String.Join(",", safeList) + "]";
		}

        void Run(string[] args)
        {
            string formatString =
                "Format: \n" +
                "  <csvfilename>\n"+
				"  [-showaverages]\n"+
				"  [-forceFullRead] (always reads the full CSV)\n" +
				"  [-quiet] (no logging. Just throws returns a non-zero error code if the CSV is bad)\n" +
				"  [-toJson <filename>]";

			// Read the command line
			if (args.Length < 1)
            {
				WriteLine("CsvInfo " + Version.Get());
                WriteLine(formatString);
                return;
            }

            string csvFilename = args[0];

            ReadCommandLine(args);

            bool showAverages = GetBoolArg("showAverages");
			bool showTotals= GetBoolArg("showTotals");
			string jsonFilename = GetArg("toJson",false);
			bool bReadJustHeader = !GetBoolArg("forceFullRead") && !showAverages && !showTotals;

			CSVStats.CsvFileInfo fileInfo = new CsvFileInfo();
			CsvStats csvStats = CsvStats.ReadCSVFile(csvFilename, null, 0, false, fileInfo, bReadJustHeader);

			if ( GetBoolArg("quiet") )
			{
				return;
			}

			if (jsonFilename != "")
			{
				// We just write the lines raw, since this version of .Net doesn't have a json serializer. 
				// TODO: Fix this when we upgrade to .Net 5.0 and use System.Text.Json
				List<string> jsonLines = new List<string>();
				jsonLines.Add("{");
				jsonLines.Add("  \"sampleCount\":" + fileInfo.SampleCount+",");

				if (csvStats.metaData != null)
				{
					jsonLines.Add("  \"metadata\": {");
					Dictionary<string, string> metadata = csvStats.metaData.Values;
					int count = metadata.Count;
					int index = 0;
					foreach (string key in metadata.Keys)
					{
						string line = "    " + ToJsonString(key) + ":" + ToJsonString(metadata[key]);
						if (index < count-1)
						{
							line += ",";
						}
						jsonLines.Add(line);
						index++;
					}
					jsonLines.Add("  },");
				}
				List<string> statLines = new List<string>();
				foreach (StatSamples stat in csvStats.Stats.Values.ToArray())
				{
					statLines.Add(stat.Name);
				}
				statLines.Sort();

				if (showTotals || showAverages)
				{
					jsonLines.Add("  \"stats\": {");
					for (int i=0; i<statLines.Count; i++)
					{
						string statName = statLines[i];
						List<string> entries = new List<string>();
						if ( showTotals )
						{
							entries.Add("\"total\":" + csvStats.GetStat(statName).total);
						}
						if (showAverages)
						{
							entries.Add("\"average\":" + csvStats.GetStat(statName).average);
						}
						string line = "    \"" + statName + "\": {" + String.Join(",", entries) + "}";
						if (i < statLines.Count-1)
							line += ",";
						jsonLines.Add(line);
					}
					jsonLines.Add("  }");
				}
				else
				{
					// Just output stats as an array if totals/averages were not requested
					jsonLines.Add("  \"stats\": " + ToJsonStringList(statLines));
				}


				jsonLines.Add("}");
				System.IO.File.WriteAllLines(jsonFilename,jsonLines);
				Console.Out.WriteLine("Wrote csv info to " + jsonFilename);
			}
			else
			{
				// Write out the sample count
				Console.Out.WriteLine("Sample Count: " + fileInfo.SampleCount);

				List<string> statLines = new List<string>();
				foreach (StatSamples stat in csvStats.Stats.Values.ToArray())
				{
					string statLine = stat.Name;
					if (showAverages)
					{
						statLine += " (" + stat.average.ToString() + ") ";
					}
					if (showTotals)
					{
						statLine += " (Total: " + stat.total.ToString()+") ";
					}
					statLines.Add(statLine);
				}
				statLines.Sort();

				// Write out the sorted stat names
				Console.Out.WriteLine("Stats:");
				foreach (string statLine in statLines)
				{
					Console.Out.WriteLine("  " + statLine);
				}

				if (csvStats.metaData != null)
				{
					// Write out the metadata, if it exists
					Console.Out.WriteLine("\nMetadata:");
					foreach (KeyValuePair<string, string> pair in csvStats.metaData.Values.ToArray())
					{
						string key = pair.Key.PadRight(20);
						Console.Out.WriteLine("  " + key + ": " + pair.Value);
					}
				}

			}
		}
    }
}
