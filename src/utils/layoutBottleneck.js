import React, { useEffect, useState } from "react";
import ReactFlow, { Background, Controls } from "reactflow";
import "reactflow/dist/style.css";
import { computeLayout } from "../layoutBottleneck";

function parseAlb(albText) {
  const lines = albText
    .split("\n")
    .map((l) => l.trim())
    .filter((l) => l.length > 0);

  const nTasksLine = lines.find((l) => l.startsWith("<number of tasks>"));
  const nTasksIndex = lines.indexOf(nTasksLine);
  const nTasks = parseInt(lines[nTasksIndex + 1]);

  const timesLine = lines.find((l) => l.startsWith("<task times>"));
  const timesIndex = lines.indexOf(timesLine);
  let taskTimes = [];
  for (let i = timesIndex + 1; i < lines.length; i++) {
    if (lines[i].startsWith("<") || lines[i] === "<end>") break;
    const parts = lines[i].split(/\s+/);
    taskTimes.push({ id: parts[0], time: parseInt(parts[1]) });
  }

  const precLine = lines.find((l) => l.startsWith("<precedence relations>"));
  const precIndex = lines.indexOf(precLine);
  let precedence = [];
  for (let i = precIndex + 1; i < lines.length; i++) {
    if (lines[i].startsWith("<") || lines[i] === "<end>") break;
    const [src, tgt] = lines[i].split(",").map((s) => s.trim());
    precedence.push({ source: src, target: tgt });
  }

  // Utiliser computeLayout pour positionner proprement les nœuds
  const { layoutedNodes, layoutedEdges } = computeLayout(
    taskTimes.map(({ id, time }) => ({
      id: id.toString(),
      data: { label: id, duration: time },
    })),
    precedence.map(({ source, target }) => ({
      source: source.toString(),
      target: target.toString(),
    }))
  );

  return {
    nodes: layoutedNodes,
    edges: layoutedEdges.map(({ source, target }) => ({
      id: `e${source}-${target}`,
      source,
      target,
      animated: true,
      style: { stroke: "#222" },
    })),
  };
}

export default function DAG({
  albText,
  assignedTasks,
  selectedTask,
  setSelectedTask,
}) {
  const [elements, setElements] = useState({ nodes: [], edges: [] });

  useEffect(() => {
    if (albText) {
      const parsed = parseAlb(albText);
      setElements(parsed);
    }
  }, [albText]);

  // Modifier les styles des noeuds selon sélection et affectation
  const nodes = elements.nodes.map((node) => {
    const isAssigned = assignedTasks.has(node.id);
    const isSelected = selectedTask === node.id;
    return {
      ...node,
      style: {
        ...node.style,
        background: isAssigned ? "#444" : isSelected ? "#f39c12" : "#9CA8B3",
        cursor: isAssigned ? "default" : "pointer",
        opacity: isAssigned ? 0.5 : 1,
        color: isAssigned ? "#ccc" : "#fff",
      },
    };
  });

  return (
  <ReactFlow
    nodes={nodes}
    edges={elements.edges.map(edge => ({
      ...edge,
      markerStart: undefined,           // Supprime la pointe au début (le rond)
      markerEnd: {
        type: 'arrowclosed',            // Flèche pleine à la fin
        color: '#b22222',               // Couleur rouge foncé (tu peux changer)
        width: 15,
        height: 15,
      },
    }))}
    fitView
    onNodeClick={(event, node) => {
      if (!assignedTasks.has(node.id)) {
        setSelectedTask(node.id);
      }
    }}
    nodesDraggable={true}
    nodesConnectable={false}
    elementsSelectable={false}
  >
    <Background />
    <Controls />
  </ReactFlow>
);

}
