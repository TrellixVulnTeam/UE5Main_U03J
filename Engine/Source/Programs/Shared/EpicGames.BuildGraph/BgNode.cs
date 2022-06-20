// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Xml;
using EpicGames.Core;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Reference to an output tag from a particular node
	/// </summary>
	public class BgNodeOutput
	{
		/// <summary>
		/// The node which produces the given output
		/// </summary>
		public BgNode ProducingNode { get; }

		/// <summary>
		/// Name of the tag
		/// </summary>
		public string TagName { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="inProducingNode">Node which produces the given output</param>
		/// <param name="inTagName">Name of the tag</param>
		public BgNodeOutput(BgNode inProducingNode, string inTagName)
		{
			ProducingNode = inProducingNode;
			TagName = inTagName;
		}

		/// <summary>
		/// Returns a string representation of this output for debugging purposes
		/// </summary>
		/// <returns>The name of this output</returns>
		public override string ToString()
		{
			return String.Format("{0}: {1}", ProducingNode.Name, TagName);
		}
	}

	/// <summary>
	/// Defines a node, a container for tasks and the smallest unit of execution that can be run as part of a build graph.
	/// </summary>
	public class BgNode
	{
		/// <summary>
		/// The node's name
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Array of inputs which this node requires to run
		/// </summary>
		public BgNodeOutput[] Inputs { get; }

		/// <summary>
		/// Array of outputs produced by this node
		/// </summary>
		public BgNodeOutput[] Outputs { get; }

		/// <summary>
		/// Nodes which this node has input dependencies on
		/// </summary>
		public BgNode[] InputDependencies { get; set; }

		/// <summary>
		/// Nodes which this node needs to run after
		/// </summary>
		public BgNode[] OrderDependencies { get; set; }

		/// <summary>
		/// Tokens which must be acquired for this node to run
		/// </summary>
		public FileReference[] RequiredTokens { get; }

		/// <summary>
		/// List of tasks to execute
		/// </summary>
		public List<BgTask> Tasks { get; } = new List<BgTask>();

		/// <summary>
		/// List of email addresses to notify if this node fails.
		/// </summary>
		public HashSet<string> NotifyUsers { get; set; } = new HashSet<string>(StringComparer.InvariantCultureIgnoreCase);

		/// <summary>
		/// If set, anyone that has submitted to one of the given paths will be notified on failure of this node
		/// </summary>
		public HashSet<string> NotifySubmitters { get; set; } = new HashSet<string>(StringComparer.InvariantCultureIgnoreCase);

		/// <summary>
		/// Whether to start this node as soon as its dependencies are satisfied, rather than waiting for all of its agent's dependencies to be met.
		/// </summary>
		public bool RunEarly { get; set; } = false;

		/// <summary>
		/// Whether to ignore warnings produced by this node
		/// </summary>
		public bool NotifyOnWarnings { get; set; } = true;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="inName">The name of this node</param>
		/// <param name="inInputs">Inputs that this node depends on</param>
		/// <param name="inOutputNames">Names of the outputs that this node produces</param>
		/// <param name="inInputDependencies">Nodes which this node is dependent on for its inputs</param>
		/// <param name="inOrderDependencies">Nodes which this node needs to run after. Should include all input dependencies.</param>
		/// <param name="inRequiredTokens">Optional tokens which must be required for this node to run</param>
		public BgNode(string inName, BgNodeOutput[] inInputs, string[] inOutputNames, BgNode[] inInputDependencies, BgNode[] inOrderDependencies, FileReference[] inRequiredTokens)
		{
			Name = inName;
			Inputs = inInputs;

			List<BgNodeOutput> allOutputs = new List<BgNodeOutput>();
			allOutputs.Add(new BgNodeOutput(this, "#" + Name));
			allOutputs.AddRange(inOutputNames.Where(x => String.Compare(x, Name, StringComparison.InvariantCultureIgnoreCase) != 0).Select(x => new BgNodeOutput(this, x)));
			Outputs = allOutputs.ToArray();

			InputDependencies = inInputDependencies;
			OrderDependencies = inOrderDependencies;
			RequiredTokens = inRequiredTokens;
		}

		/// <summary>
		/// Returns the default output for this node, which includes all build products
		/// </summary>
		public BgNodeOutput DefaultOutput => Outputs[0];

		/// <summary>
		/// Determines the minimal set of direct input dependencies for this node to run
		/// </summary>
		/// <returns>Sequence of nodes that are direct inputs to this node</returns>
		public IEnumerable<BgNode> GetDirectInputDependencies()
		{
			HashSet<BgNode> directDependencies = new HashSet<BgNode>(InputDependencies);
			foreach (BgNode inputDependency in InputDependencies)
			{
				directDependencies.ExceptWith(inputDependency.InputDependencies);
			}
			return directDependencies;
		}

		/// <summary>
		/// Determines the minimal set of direct order dependencies for this node to run
		/// </summary>
		/// <returns>Sequence of nodes that are direct order dependencies of this node</returns>
		public IEnumerable<BgNode> GetDirectOrderDependencies()
		{
			HashSet<BgNode> directDependencies = new HashSet<BgNode>(OrderDependencies);
			foreach (BgNode orderDependency in OrderDependencies)
			{
				directDependencies.ExceptWith(orderDependency.OrderDependencies);
			}
			return directDependencies;
		}

		/// <summary>
		/// Write this node to an XML writer
		/// </summary>
		/// <param name="writer">The writer to output the node to</param>
		public void Write(XmlWriter writer)
		{
			writer.WriteStartElement("Node");
			writer.WriteAttributeString("Name", Name);

			string[] requireNames = Inputs.Select(x => x.TagName).ToArray();
			if (requireNames.Length > 0)
			{
				writer.WriteAttributeString("Requires", String.Join(";", requireNames));
			}

			string[] producesNames = Outputs.Where(x => x != DefaultOutput).Select(x => x.TagName).ToArray();
			if (producesNames.Length > 0)
			{
				writer.WriteAttributeString("Produces", String.Join(";", producesNames));
			}

			string[] afterNames = GetDirectOrderDependencies().Except(InputDependencies).Select(x => x.Name).ToArray();
			if (afterNames.Length > 0)
			{
				writer.WriteAttributeString("After", String.Join(";", afterNames));
			}

			if (!NotifyOnWarnings)
			{
				writer.WriteAttributeString("NotifyOnWarnings", NotifyOnWarnings.ToString());
			}

			if (RunEarly)
			{
				writer.WriteAttributeString("RunEarly", RunEarly.ToString());
			}

			foreach (BgTask task in Tasks)
			{
				task.Write(writer);
			}
			writer.WriteEndElement();
		}

		/// <summary>
		/// Returns the name of this node
		/// </summary>
		/// <returns>The name of this node</returns>
		public override string ToString()
		{
			return Name;
		}
	}
}
