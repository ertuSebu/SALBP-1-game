import React, { useState, useEffect } from "react";
import DAG from "./components/DAG";

async function fetchText(path) {
  const res = await fetch(path);
  if (!res.ok) throw new Error(`Error loading ${path}`);
  return res.text();
}

function parseTaskTimes(albText) {
  const lines = albText.split("\n").map((l) => l.trim());
  const idx = lines.findIndex((l) => l === "<task times>");
  if (idx === -1) return {};
  const times = {};
  for (let i = idx + 1; i < lines.length; i++) {
    if (lines[i].startsWith("<")) break;
    if (!lines[i]) continue;
    const parts = lines[i].split(/\s+/);
    if (parts.length < 2) continue;
    const id = parts[0];
    const dur = parseInt(parts[1], 10);
    if (!id || isNaN(dur)) continue;
    times[id] = dur;
  }
  return times;
}

function parseCycleTime(albText) {
  const match = albText.match(/<cycle time>\s*(\d+)/);
  return match ? parseInt(match[1], 10) : 1000;
}

function parsePrecedenceRelations(albText) {
  const lines = albText.split("\n").map((l) => l.trim()).filter(Boolean);
  const idx = lines.findIndex((l) => l === "<precedence relations>");
  if (idx === -1) return {};
  const prec = {};
  for (let i = idx + 1; i < lines.length; i++) {
    if (lines[i].startsWith("<") || lines[i] === "<end>") break;
    const [src, tgt] = lines[i].split(",").map((s) => s.trim());
    if (!prec[tgt]) prec[tgt] = [];
    prec[tgt].push(src);
  }
  return prec;
}

async function loadSolution(filename) {
  try {
    const res = await fetch(filename);
    if (!res.ok) throw new Error("Solution file not found");
    const text = await res.text();
    const stations = [];
    const lines = text.split("\n");
    for (const line of lines) {
      const match = line.match(/^station_(\d+):\s*(.+)$/i);
      if (match) {
        const id = parseInt(match[1], 10);
        const tasks = match[2].trim().split(/\s+/).filter(Boolean);
        stations.push({ id, tasks });
      }
    }
    return stations;
  } catch (e) {
    console.error("Error loading solution:", e);
    return null;
  }
}

export default function App() {
  const [albText, setAlbText] = useState("");
  const [stations, setStations] = useState([]);
  const [selectedTask, setSelectedTask] = useState(null);
  const [assignedTasks, setAssignedTasks] = useState(new Set());
  const [optimalStations, setOptimalStations] = useState(null);
  const [showSolution, setShowSolution] = useState(false);
  const [listGraphs, setListGraphs] = useState([]);
  const [currentGraph, setCurrentGraph] = useState("i1");

  useEffect(() => {
    fetch("/instance/list.json")
      .then((r) => r.json())
      .then((data) => {
        const cleaned = data.map((name) =>
          name.endsWith(".alb") ? name.slice(0, -4) : name
        );
        setListGraphs(cleaned);
      })
      .catch((e) => console.error("Error loading list.json:", e));
  }, []);

  useEffect(() => {
    if (!currentGraph) return;
    fetch(`/instance/${currentGraph}.alb`)
      .then((r) => {
        if (!r.ok) throw new Error(".alb file not found");
        return r.text();
      })
      .then((text) => {
        setAlbText(text);
        setStations([]);
        setAssignedTasks(new Set());
        setOptimalStations(null);
        setShowSolution(false);
        setSelectedTask(null);
      })
      .catch((e) => {
        console.error("Error loading .alb:", e);
        setAlbText("");
      });
  }, [currentGraph]);

  const taskTimes = parseTaskTimes(albText);
  const cycleTime = parseCycleTime(albText);
  const precedence = parsePrecedenceRelations(albText);
  const allTasksAssigned =
    Object.keys(taskTimes).length > 0 &&
    Object.keys(taskTimes).every((t) => assignedTasks.has(t));

  function addStation() {
    setStations((s) => [...s, { id: s.length + 1, tasks: [] }]);
  }

  function removeStation(stationId) {
    const station = stations.find((s) => s.id === stationId);
    if (!station) return;
    if (station.tasks.length > 0) {
      alert("You cannot delete a station that contains tasks.");
      return;
    }
    setStations((s) => s.filter((st) => st.id !== stationId));
  }

  function assignTaskToStation(stationId) {
    if (!selectedTask) return;

    const preds = precedence[selectedTask] || [];
    const allPredsAssigned = preds.every((p) => assignedTasks.has(p));
    if (!allPredsAssigned) {
      alert(`Cannot assign task ${selectedTask}: precedence not respected.`);
      return;
    }

    const selectedDuration = taskTimes[selectedTask];
    const station = stations.find((s) => s.id === stationId);
    const currentSum = station.tasks.reduce(
      (sum, t) => sum + (taskTimes[t] || 0),
      0
    );
    if (currentSum + selectedDuration > cycleTime) {
      alert(
        `Cannot add task ${selectedTask}: total (${currentSum +
          selectedDuration}) exceeds cycle time (${cycleTime}).`
      );
      return;
    }

    if (!assignedTasks.has(selectedTask)) {
      setStations((s) =>
        s.map((st) =>
          st.id === stationId ? { ...st, tasks: [...st.tasks, selectedTask] } : st
        )
      );
      setAssignedTasks((at) => new Set(at).add(selectedTask));
      setSelectedTask(null);
    }
  }

  function removeTaskFromStation(stationId, taskId) {
    setStations((s) =>
      s.map((st) =>
        st.id === stationId
          ? { ...st, tasks: st.tasks.filter((t) => t !== taskId) }
          : st
      )
    );
    setAssignedTasks((at) => {
      const newSet = new Set(at);
      newSet.delete(taskId);
      return newSet;
    });
  }

  async function loadAndShowSolution() {
    if (!allTasksAssigned) {
      alert("Please assign all tasks before validating.");
      return;
    }
    const solFile = `/soluce/${currentGraph}.sol`;
    const sol = await loadSolution(solFile);
    if (!sol) {
      alert("Could not load optimal solution.");
      return;
    }
    setOptimalStations(sol);
    setShowSolution(true);
  }

  function changeGraph() {
    if (listGraphs.length === 0) {
      alert("Graph list is empty.");
      return;
    }
    let nextGraph = currentGraph;
    while (nextGraph === currentGraph) {
      nextGraph =
        listGraphs[Math.floor(Math.random() * listGraphs.length)];
    }
    setCurrentGraph(nextGraph);
  }

  const playerStationCount = stations.length;
  const optimalStationCount = optimalStations ? optimalStations.length : null;
  const playerHasOptimal =
    optimalStationCount !== null && playerStationCount === optimalStationCount;

  return (
    <div style={{ padding: 20 }}>
      <h1>SALBP-1 Game - Task Assignment </h1>
      <p>
        <b>Current graph:</b> {currentGraph}.alb
      </p>
      <p>
        <b>Cycle time:</b> {cycleTime}
      </p>

      <button onClick={changeGraph} style={{ marginBottom: 10 }}>
        🔄 Change to random graph
      </button>

      <div style={{ border: "1px solid #ccc", height: 300, marginBottom: 20 }}>
        <DAG
          albText={albText}
          assignedTasks={assignedTasks}
          selectedTask={selectedTask}
          setSelectedTask={setSelectedTask}
        />
      </div>

      <button onClick={() => setStations([])}>Reset stations</button>
      <button onClick={addStation} style={{ marginLeft: 10 }}>
        + Add a station
      </button>

      <button
        onClick={loadAndShowSolution}
        disabled={!allTasksAssigned}
        style={{
          marginLeft: 10,
          backgroundColor: allTasksAssigned ? "#4CAF50" : "#ccc",
          color: allTasksAssigned ? "white" : "#666",
          cursor: allTasksAssigned ? "pointer" : "not-allowed",
        }}
        title={
          !allTasksAssigned
            ? "Assign all tasks first"
            : "Show optimal solution"
        }
      >
        ✅ Validate solution
      </button>

      <h2>Your solution ({playerStationCount} stations)</h2>
      <div style={{ display: "flex", gap: 20, flexWrap: "wrap" }}>
        {stations.map(({ id, tasks }) => {
          const total = tasks.reduce((sum, t) => sum + (taskTimes[t] || 0), 0);
          return (
            <div
              key={id}
              onClick={() => assignTaskToStation(id)}
              style={{
                border: "1px solid #444",
                padding: 10,
                minWidth: 150,
                cursor: "pointer",
                userSelect: "none",
              }}
            >
              <h3>Station {id}</h3>
              <button
                onClick={(e) => {
                  e.stopPropagation();
                  removeStation(id);
                }}
                style={{
                  float: "right",
                  background: "transparent",
                  border: "none",
                  color: "red",
                  fontSize: "1.2em",
                  cursor: "pointer",
                }}
                title="Delete station"
              >
                🗑️
              </button>

              <p style={{ fontSize: "0.8em" }}>
                Total: {total} / {cycleTime}
              </p>
              {tasks.length === 0 ? (
                <i>(Click here to assign selected task)</i>
              ) : (
                tasks.map((t) => (
                  <div
                    key={t}
                    style={{
                      background: "#eee",
                      margin: 4,
                      padding: 4,
                      borderRadius: 4,
                      display: "flex",
                      justifyContent: "space-between",
                      alignItems: "center",
                    }}
                  >
                    <span>
                      Task {t} ({taskTimes[t]})
                    </span>
                    <button
                      onClick={(e) => {
                        e.stopPropagation();
                        removeTaskFromStation(id, t);
                      }}
                      style={{
                        background: "transparent",
                        border: "none",
                        color: "red",
                        fontSize: "1em",
                        cursor: "pointer",
                        marginLeft: 8,
                      }}
                      title="Remove task"
                    >
                      🗑️
                    </button>
                  </div>
                ))
              )}
            </div>
          );
        })}
      </div>

      {showSolution && optimalStations && (
        <>
          <h2>
            Optimal solution ({optimalStationCount} stations){" "}
            {playerHasOptimal ? (
              <span style={{ color: "green", fontWeight: "bold" }}>✅</span>
            ) : (
              <span style={{ color: "red", fontWeight: "bold" }}>❌</span>
            )}
          </h2>
          <div style={{ display: "flex", gap: 20, flexWrap: "wrap" }}>
            {optimalStations.map(({ id, tasks }) => {
              const total = tasks.reduce((sum, t) => sum + (taskTimes[t] || 0), 0);
              return (
                <div
                  key={id}
                  style={{
                    border: "2px solid green",
                    padding: 10,
                    minWidth: 150,
                    backgroundColor: "#e0ffe0",
                  }}
                >
                  <h3>Station {id}</h3>
                  <p style={{ fontSize: "0.8em" }}>
                    Total: {total} / {cycleTime}
                  </p>
                  {tasks.map((t) => (
                    <div
                      key={t}
                      style={{
                        background: "#ccffcc",
                        margin: 4,
                        padding: 4,
                        borderRadius: 4,
                      }}
                    >
                      Task {t} ({taskTimes[t]})
                    </div>
                  ))}
                </div>
              );
            })}
          </div>
        </>
      )}
    </div>
  );
}
