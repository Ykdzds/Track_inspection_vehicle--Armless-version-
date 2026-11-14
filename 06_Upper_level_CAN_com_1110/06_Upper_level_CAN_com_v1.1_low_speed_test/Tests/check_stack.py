import re
import sys
from pathlib import Path

html = Path(sys.argv[1]).read_text(encoding="utf-8")
config = Path(sys.argv[2]).read_text(encoding="utf-8")
debug = int(sys.argv[3])
sections = re.findall(r'<P><STRONG><a name="\[([^]]+)\]"></a>(.*?)</STRONG>(.*?)(?=<P><STRONG>|\Z)', html, re.S)
nodes = {}
identifiers = {}
for identifier, name, body in sections:
    identifiers[identifier] = name
    size = re.search(r'Stack size (\d+) bytes', body)
    calls = re.search(r'\[Calls\](.*?)(?=\[Called By\]|\[Address|\Z)', body, re.S)
    nodes[name] = [int(size[1]) if size else 0, re.findall(r'href="#\[([^]]+)\]"', calls[1]) if calls else []]
for name, node in nodes.items():
    node[1] = [identifiers[target] for target in node[1] if target in identifiers]
if "UART_EndRxTransfer" in nodes and nodes["UART_EndRxTransfer"][0] == 0:
    nodes["UART_EndRxTransfer"][1] = [child for child in nodes["UART_EndRxTransfer"][1] if child != "UART_EndRxTransfer"]
for name in ("Send", "SendCommand", "SendSpeed", "UpperControl_Tick"):
    if name in nodes:
        nodes[name][1].append("ProtocolSend")
if debug and "Event" in nodes:
    nodes["Event"][1].append("LogEvent")

def depth(name, active=()):
    if name in active:
        raise RuntimeError("Unresolved reachable stack cycle: " + str(active + (name,)))
    size, children = nodes[name]
    return size + max((depth(child, active + (name,)) for child in children), default=0)

tasks = [("ProtocolTask", "PROTOCOL"), ("PeriodicTask", "PERIODIC"), ("OledTask", "OLED"), ("HealthTask", "HEALTH"), ("KeyTask", "KEY")]
if debug:
    tasks.append(("LogTask", "LOG"))
for function, setting in tasks:
    allocated = 4 * int(re.search(r'#define UPPER_' + setting + r'_STACK_WORDS\s+(\d+)U', config)[1])
    bound = depth(function) + 72
    margin = allocated - bound
    print(f"{function}: allocated={allocated} graph+callbacks+context={bound} margin={margin}")
    if margin < 256:
        raise RuntimeError("Static known margin below 256: " + function)

rtos = (Path(sys.argv[2]).parents[1] / "Core/Inc/FreeRTOSConfig.h").read_text(encoding="utf-8")
for function, setting in (("prvIdleTask", "configMINIMAL_STACK_SIZE"), ("prvTimerTask", "configTIMER_TASK_STACK_DEPTH")):
    allocated = 4 * int(re.search(r'#define ' + setting + r'[^\r\n]*?([0-9]+)\)?\s*(?:\r?\n|$)', rtos)[1])
    bound = depth(function) + 72
    print(f"{function}: allocated={allocated} graph+context={bound} margin={allocated-bound}; no application timer/idle callbacks configured")
    if allocated - bound < 256:
        raise RuntimeError("Kernel task static margin below 256: " + function)
