/*
/// Provides functions to set values on the debug tree
module "debug"
*///

/**************************************************************************
*   Copyright 2018 Michael Eischer, Philipp Nordhus, Andreas Wendler      *
*   Robotics Erlangen e.V.                                                *
*   http://www.robotics-erlangen.de/                                      *
*   info@robotics-erlangen.de                                             *
*                                                                         *
*   This program is free software: you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation, either version 3 of the License,  ||      *
*   any later version.                                                    *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY  ||  FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU General Public License     *
*   along with this program.  if not, see <http://www.gnu.org/licenses/>.*
**************************************************************************///

declare var amun: any;
let addDebug:Function = amun.addDebug;
import {log} from "base/globals";

let debugStack: string[] = [""];

let joinCache: {[prefix: string]: {[name: string]: string}} = {};

function prefixName (name?: string): string {
	let prefix = debugStack[debugStack.length-1];
	if (name == undefined) {
		return prefix;
	} else if (prefix.length == 0) {
		return name;
	}

	// caching to avoid joining the debug keys over and over
	if (joinCache[prefix] != null && joinCache[prefix][name] != null) {
		return joinCache[prefix][name];
	}
	let joined = prefix + "/" + name;
	if (joinCache[prefix] == undefined) {
		joinCache[prefix] = {};
	}
	joinCache[prefix][name] = joined;
	return joined;
}

/// Pushes a new key on the debug stack.
// @name push
// @param name string - Name of the new subtree
// @param [value string - Value for the subtree header]
export function push (name: string, value?: string) {
	debugStack.push(prefixName(name));
	if (value != undefined) {
		set(undefined, value);
	}
}

/// Pushes a root key on the debug stack.
// @name pushtop
// @param name string - Name of the new root tree or nil to push root
export function pushtop (name?: string) {
	if (!name) {
		debugStack.push("");
	} else {
		debugStack.push(name);
	}
}

/// Pops last key from the debug stack.
// @name pop
export function pop () {
	if (debugStack.length > 0) {
		debugStack.pop();
	}
}

/// Get extra params for debug.set.
// This can be used to keep the table # stable across calls to debug.set
// Usage: let extraParams = debug.getInitialExtraParams()
// debug.set(key, value, unpack(extraParams))
// @name getInitialExtraParams
// @return Initial extra params
export function getInitialExtraParams (): object {
	let visited = {};
	let tableCounter = [0];
	return { visited, tableCounter };
}


/// Sets value for the given name.
// if (value is nil store it as text
// For the special value nil the value is set for the current key
// @name set
// @param name string - Name of the value
// @param value string - Value to set
export function set (name: string|undefined, value: any, visited: Map<object, string> = new Map(), tableCounter?: number[]) {
	// visited and tableCounter must be compatible with getInitialExtraParams

	let result: any;
	if (typeof(value) == "object") {
		if (visited.get(value)) {
			set(name, visited.get(value) + " (duplicate)");
			return;
		}
		let suffix = "";
		if (tableCounter) {
			suffix = " [#"+String(tableCounter[0])+"]";
			tableCounter[0] = tableCounter[0] + 1;
		}
		visited.set(value, suffix);

		// custom toString for Vector, Robot
		if (value._toString) {
			let origValue = value;
			result = value._toString() + suffix;
			visited.set(origValue, result);
		} else {
			let friendlyName;
			if (value.constructor != undefined && Object.keys(value).length === 0) {
				friendlyName = "empty object";
			} else if (value.constructor != undefined) {
				friendlyName = value.constructor.name;
			} else {
				friendlyName = "";
			}

			push(String(name));
			friendlyName = friendlyName+suffix;
			set(undefined, friendlyName);
			visited.set(value, friendlyName);

			let entryCounter = 1;
			for (let k in value) {
				let v = value[k];
				set(String(k), v, visited, tableCounter);
			}
			pop();
			return;
		}
	} else if (typeof(value) == "function") {
		result = "function " + value.name;
	} else {
		result = value;
	}

	addDebug(prefixName(name), result);
}

/// Clears the debug stack
// @name resetStack
export function resetStack () {
	if (debugStack.length != 0 || debugStack[0] != "") {
		log("Unbalanced push/pop on debug stack");
		for (let v in debugStack) {
			log(v);
		}
	}
	debugStack = [""];
}