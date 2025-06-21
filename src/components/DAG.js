import React, { useEffect, useState, useCallback } from "react";
import ReactFlow, {
  Background,
  Controls,
  applyNodeChanges,
} from "reactflow";
import "reactflow/dist/style.css";

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
    const parts = lines[i].split(" ");
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

  // Positionnement simple en ligne horizontale
  const nodes = taskTimes.map(({ id, time }) => ({
    id: id.toString(),
    data: { label: `${id} (${time})` },
    position: { x: parseInt(id) * 100, y: 100 },
    style: {
      width: 50,
      height: 50,
      borderRadius: "50%",
      background: "#9CA8B3",
      color: "#fff",
      textAlign: "center",
      lineHeight: "50px",
      cursor: "pointer",
    },
  }));

  // Ajout d'un markerEnd personnalisé sur chaque edge
  
  const edges = precedence.map(({ source, target }, i) => ({
    id: `e${source}-${target}`,
    source: source.toString(),
    target: target.toString(),
    animated: true,
    style: { stroke: "#222" },
    markerEnd: {
      type: "arrowclosed",
      color: "#b22222", // rouge foncé
      width: 25,        // largeur agrandie
      height: 25,       // hauteur agrandie
    },
  }));



  return { nodes, edges };
}

export default function DAG({
  albText,
  assignedTasks,
  selectedTask,
  setSelectedTask,
}) {
  const [nodes, setNodes] = useState([]);
  const [edges, setEdges] = useState([]);

  useEffect(() => {
    if (albText) {
      const parsed = parseAlb(albText);
      setNodes(parsed.nodes);
      setEdges(parsed.edges);
    }
  }, [albText]);

  const onNodesChange = useCallback(
    (changes) => {
      setNodes((nds) => applyNodeChanges(changes, nds));
    },
    [setNodes]
  );

  const styledNodes = nodes.map((node) => {
    const isAssigned = assignedTasks.has(node.id);
    const isSelected = selectedTask === node.id;
    return {
      ...node,
      style: {
        ...node.style,
        background: isAssigned ? "#444" : isSelected ? "#f39c12" : "#9CA8B3",
        cursor: isAssigned ? "default" : "pointer",
        opacity: isAssigned ? 0.5 : 1,
        color: isAssigned ? "#ccc" : "red",
      },
    };
  });

  return (
    <ReactFlow
      nodes={styledNodes}
      edges={edges}
      fitView
      onNodeClick={(event, node) => {
        if (!assignedTasks.has(node.id)) {
          setSelectedTask(node.id);
        }
      }}
      onNodesChange={onNodesChange}
      nodesDraggable={true}
      nodesConnectable={false}
      elementsSelectable={false}
    >
      <Background />
      <Controls />
    </ReactFlow>
  );
}
