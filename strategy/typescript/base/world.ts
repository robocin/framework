/**
 * @module world
 * Provides informations about game state
 */

/**************************************************************************
*   Copyright 2018 Alexander Danzer, Michael Eischer, Tobias Heineken     *
*                  Christian Lobmeier, Philipp Nordhus, Andreas Wendler   *
*   Robotics Erlangen e.V.                                                *
*   http://www.robotics-erlangen.de/                                      *
*   info@robotics-erlangen.de                                             *
*                                                                         *
*   This program is free software: you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation, either version 3 of the License, or     *
*   any later version.                                                    *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU General Public License     *
*   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
**************************************************************************/

let amunLocal = amun;
import { Ball as BallClass } from "base/ball";
import * as Constants from "base/constants";
import { Coordinates } from "base/coordinates";
import * as MathUtil from "base/mathutil";
// let mixedTeam = require "base/mixedteam"
import * as pb from "base/protobuf";
import { FriendlyRobot, Robot } from "base/robot";
import { AbsTime, RelTime } from "base/timing";
import { Position, Vector } from "base/vector";


/** Current unix timestamp in seconds (with nanoseconds precision) */
export let Time: AbsTime = 0;
/** Time since last update */
export let TimeDiff: RelTime = 0;
export let AoI: pb.world.TrackingAOI | undefined = undefined;
/** current Ball */
export let Ball: BallClass = new BallClass();
/** List of own robots in an arbitary order */
export let FriendlyRobots: FriendlyRobot[] = [];
/** Own robots which currently aren't tracked */
export let FriendlyInvisibleRobots: FriendlyRobot[] = [];
/** List of own robots with robot id as index */
export let FriendlyRobotsById: {[index: number]: FriendlyRobot} = {};
/** List of all own robots in an arbitary order */
export let FriendlyRobotsAll: FriendlyRobot[] = [];
/** Own keeper if on field or nil */
export let FriendlyKeeper: FriendlyRobot | undefined;
/** List of opponent robots in an arbitary order */
export let OpponentRobots: Robot[] = [];
/** List of opponent robots with robot id as index */
export let OpponentRobotsById: {[index: number]: Robot} = {};
/** Opponent keeper if on field or nil */
export let OpponentKeeper: Robot | undefined;
/** Every visible robot in an arbitary order */
export let Robots: Robot[] = [];
/** True if we are the blue team, otherwise we're yellow */
export let TeamIsBlue: boolean = false;
/** True if the world is simulated */
export let IsSimulated: boolean = false;
/** True if playing on the large field */
export let IsLargeField: boolean = false;
/** True if the current strategy run is a replay run */
export let IsReplay: boolean = false;
/**
 * Mixed team data sent by partner team, indexed by robot id, only set if data was received;
 * Has the following fields: role string (values: Default, Goalie, Defense, Offense), targetPos* vector,
 * targetDir* number, shootPos* vector, * = optional
 */
export let MixedTeam: pb.ssl.TeamPlan | undefined = undefined;
export let SelectedOptions = undefined;

/**
 * current refereestate, can be one of these:
 * Halt, Stop, Game, GameForce,
 * KickoffOffensivePrepare, KickoffDefensivePrepare, KickoffOffensive, KickoffDefensive,
 * PenaltyOffensivePrepare, PenaltyDefensivePrepare, PenaltyOffensive, PenaltyDefensive,
 * DirectOffensive, DirectDefensive, IndirectOffensive, IndirectDefensive,
 * TimeoutOffensive, TimeoutDefensive, BallPlacementOffensive, BallPlacementDefensive
 */
export let RefereeState: string = "";
/**
 * current game stage, can be one of these:
 * FirstHalfPre, FirstHalf, HalfTime, SecondHalfPre, SecondHalf,
 * ExtraTimeBreak, ExtraFirstHalfPre, ExtraFirstHalf, ExtraHalfTime, ExtraSecondHalfPre, ExtraSecondHalf,
 * PenaltyShootoutBreak, PenaltyShootout, PostGame
 */
export let GameStage: string = "";
/** List of the remaining times for all active friendly yellow cards */
export let FriendlyYellowCards: number[] = [];
/** List of the remaining times for all active opponent yellow cards */
export let OpponentYellowCards: number[] = [];
/** number of red cards received for the own team */
export let FriendlyRedCards: number = 0;
/** number of red cards the opponent received */
export let OpponentRedCards: number = 0;
/** where the ball has to be placed */
export let BallPlacementPos: Readonly<Position> | undefined;

export let RULEVERSION: string = "";

/** Field geometry. Lengths in meter */
export interface GeometryType {
	/** Width of the playing field (short side) */
	FieldWidth: number;
	/** Height of the playing field (long side) */
	FieldHeight: number;
	/** Half width of the playing field (short side) */
	FieldWidthHalf: number;
	/** Half height of the playing field (long side) */
	FieldHeightHalf: number;
	/** Quarter width of the playing field (short side) */
	FieldWidthQuarter: number;
	/** Quarter height of the playing field (long side) */
	FieldHeightQuarter: number;
	/** Inner width of the goals */
	GoalWidth: number;
	/** Width of the goal walls */
	GoalWallWidth: number;
	/** Depth of the goal */
	GoalDepth: number;
	/** Height of the goals */
	GoalHeight: number;
	/** Width of the game field lines */
	LineWidth: number;
	/** Radius of the center circle */
	CenterCircleRadius: number;
	/** Distance to keep to opponent defense area during a freekick */
	FreeKickDefenseDist: number;
	/** Radius of the defense area corners (pre 2018) */
	DefenseRadius: number;
	/** Distance between the defense areas quarter circles (pre 2018) */
	DefenseStretch: number;
	/** Half distance between the defense areas quarter circles (pre 2018) */
	DefenseStretchHalf: number;
	/** Width of the rectangular defense area (longer side) (since 2018) */
	DefenseWidth: number;
	/** Half width of the rectangular defense area (longer side) (since 2018) */
	DefenseWidthHalf: number;
	/** Height of the rectangular defense area (shorter side) (since 2018) */
	DefenseHeight: number;
	/** Position of our own penalty spot */
	FriendlyPenaltySpot: Readonly<Position>;
	/** Position of the opponent's penalty spot */
	OpponentPenaltySpot: Readonly<Position>;
	/** Maximal distance from centerline during an offensive penalty */
	PenaltyLine: number;
	/** Maximal distance from centerline during an defensive penalty */
	OwnPenaltyLine: number;
	/** Center point of the goal on the line */
	FriendlyGoal: Readonly<Position>;
	/** Left side of the goal when oriented towards the opponent goal */
	FriendlyGoalLeft: Readonly<Position>;
	/** Right side of the goal when oriented towards the opponent goal */
	FriendlyGoalRight: Readonly<Position>;
	/** Center point of the goal on the line */
	OpponentGoal: Readonly<Position>;
	/** Left side of the goal when oriented towards the friendly goal */
	OpponentGoalLeft: Readonly<Position>;
	/** Right side of the goal when oriented towards the friendly goal */
	OpponentGoalRight: Readonly<Position>;
	/** Free distance around the playing field */
	BoundaryWidth: number;
	/** Width of area reserved for referee */
	RefereeWidth: number;
}

// it is guaranteed to be set before being read, so casting is fine
export let Geometry: Readonly<GeometryType> = <GeometryType> {};

// initializes Team and Geometry data
export function _init() {
	TeamIsBlue = amunLocal.isBlue();
	let geom = amunLocal.getGeometry();
	_updateGeometry(geom);
	_updateRuleVersion(geom);
	_updateTeam(amunLocal.getTeam());
}

/**
 * Update world state.
 * Has to be called once each frame
 * @return false if no vision data was received since strategy start
 */
export function update() {
	if (SelectedOptions == undefined) {
		// TODO: getSelectedOptions is not yet implemented for typescript
		// SelectedOptions = amunLocal.getSelectedOptions();
	}
	let hasVisionData = _updateWorld(amunLocal.getWorldState());
	_updateGameState(amunLocal.getGameState());
	_updateUserInput(amunLocal.getUserInput());
	IsReplay = amunLocal.isReplay ? amunLocal.isReplay() : false;
	return hasVisionData;
}

/** Creates generation specific robot object for own team */
export function _updateTeam(state: pb.robot.Team) {
	let friendlyRobotsById: {[index: number]: FriendlyRobot} = {};
	let friendlyRobotsAll: FriendlyRobot[] = [];
	for (let rdata of state.robot || []) {
		let robot = new FriendlyRobot(rdata); // No generation types for now
		friendlyRobotsById[rdata.id] = robot;
		friendlyRobotsAll.push(robot);
	}
	FriendlyRobotsById = friendlyRobotsById;
	FriendlyRobotsAll = friendlyRobotsAll;
}

/** Get rule version from geometry */
export function _updateRuleVersion(geom: pb.world.Geometry) {
	if (geom.type == undefined || geom.type === "TYPE_2014") {
		RULEVERSION = "2017";
	} else {
		RULEVERSION = "2018";
	}
}

// Setup field geometry
function _updateGeometry(geom: pb.world.Geometry) {
	let wgeom = <GeometryType> Geometry;
	wgeom.FieldWidth = geom.field_width;
	wgeom.FieldWidthHalf = geom.field_width / 2;
	wgeom.FieldWidthQuarter = geom.field_width / 4;
	wgeom.FieldHeight = geom.field_height;
	wgeom.FieldHeightHalf = geom.field_height / 2;
	wgeom.FieldHeightQuarter = geom.field_height / 4;

	wgeom.GoalWidth = geom.goal_width;
	wgeom.GoalWallWidth = geom.goal_wall_width;
	wgeom.GoalDepth = geom.goal_depth;
	wgeom.GoalHeight = geom.goal_height;

	wgeom.LineWidth = geom.line_width;
	wgeom.CenterCircleRadius = geom.center_circle_radius;
	wgeom.FreeKickDefenseDist = geom.free_kick_from_defense_dist;

	wgeom.DefenseRadius = geom.defense_radius;
	wgeom.DefenseStretch = geom.defense_stretch;
	wgeom.DefenseStretchHalf = geom.defense_stretch / 2;
	wgeom.DefenseWidth = geom.defense_width != undefined ? geom.defense_width : geom.defense_stretch;
	wgeom.DefenseHeight = geom.defense_height != undefined ? geom.defense_height : geom.defense_radius;
	wgeom.DefenseWidthHalf = (geom.defense_width != undefined ? geom.defense_width : geom.defense_stretch) / 2;

	wgeom.FriendlyPenaltySpot = Vector.createReadOnly(0, - wgeom.FieldHeightHalf + geom.penalty_spot_from_field_line_dist);
	wgeom.OpponentPenaltySpot = Vector.createReadOnly(0, wgeom.FieldHeightHalf - geom.penalty_spot_from_field_line_dist);
	wgeom.PenaltyLine = wgeom.OpponentPenaltySpot.y - geom.penalty_line_from_spot_dist;
	wgeom.OwnPenaltyLine = wgeom.FriendlyPenaltySpot.y + geom.penalty_line_from_spot_dist;

	// The goal posts are on the field lines
	wgeom.FriendlyGoal = Vector.createReadOnly(0, - wgeom.FieldHeightHalf);
	wgeom.FriendlyGoalLeft = Vector.createReadOnly(- wgeom.GoalWidth / 2, wgeom.FriendlyGoal.y);
	wgeom.FriendlyGoalRight = Vector.createReadOnly(wgeom.GoalWidth / 2, wgeom.FriendlyGoal.y);

	wgeom.OpponentGoal = Vector.createReadOnly(0, wgeom.FieldHeightHalf);
	wgeom.OpponentGoalLeft = Vector.createReadOnly(- wgeom.GoalWidth / 2, wgeom.OpponentGoal.y);
	wgeom.OpponentGoalRight = Vector.createReadOnly(wgeom.GoalWidth / 2, wgeom.OpponentGoal.y);

	wgeom.BoundaryWidth = geom.boundary_width;
	wgeom.RefereeWidth = geom.referee_width;

	IsLargeField = wgeom.FieldWidth > 5 && wgeom.FieldHeight > 7;
}

export function _updateWorld(state: pb.world.State) {
	// Get time
	if (Time != undefined) {
		TimeDiff = state.time * 1E-9 - Time;
	} else {
		TimeDiff = 0;
	}
	Time = state.time * 1E-9;
	MathUtil.randomseed(state.time);
	if (Time <= 0) {
		throw new Error("Invalid Time. Outdated ra version!");
	}
	if (IsSimulated !== state.is_simulated) {
		IsSimulated = !!state.is_simulated;
		Constants.switchSimulatorConstants(IsSimulated);
	}

	let radioResponses: pb.robot.RadioResponse[] = state.radio_response || [];

	// update ball if available
	if (state.ball) {
		Ball._update(state.ball, Time);
	}

	let dataFriendly = TeamIsBlue ? state.blue : state.yellow;
	if (dataFriendly) {
		// sort data by robot id
		let dataById: {[id: number]: pb.world.Robot} = {};
		for (let rdata of dataFriendly) {
			dataById[rdata.id] = rdata;
		}

		// Update data of every own robot
		FriendlyRobots = [];
		FriendlyInvisibleRobots = [];
		for (let robot of FriendlyRobotsAll) {
			// get responses for the current robot
			// these are identified by the robot generation and id
			let robotResponses: pb.robot.RadioResponse[] = [];
			for (let response of radioResponses) {
				if (response.generation === robot.generation
						&&  response.id === robot.id) {
					robotResponses.push(response);
				}
			}

			robot._update(dataById[robot.id], Time, robotResponses);
			robot._updatePathBoundaries(Geometry, AoI);
			// sort robot into visible / not visible
			if (robot.isVisible) {
				FriendlyRobots.push(robot);
			} else {
				FriendlyInvisibleRobots.push(robot);
			}
		}
	}

	let dataOpponent = TeamIsBlue ? state.yellow : state.blue;
	if (dataOpponent) {
		// only keep robots that are still existent
		let opponentRobotsById = OpponentRobotsById;
		OpponentRobots = [];
		OpponentRobotsById = {};
		// just update every opponent robot
		// robots that are invisible for more than one second are dropped by amun
		for (let rdata of dataOpponent) {
			let robot = opponentRobotsById[rdata.id];
			delete opponentRobotsById[rdata.id];
			if (!robot) {
				robot = new Robot(rdata.id);
			}
			robot._updateOpponent(rdata, Time);
			OpponentRobots.push(robot);
			OpponentRobotsById[rdata.id] = robot;
		}
		// mark dropped robots as invisible
		for (let robotId in opponentRobotsById) {
			opponentRobotsById[robotId]._updateOpponent(undefined, Time);
		}
	}

	Robots = FriendlyRobots.slice();
	Robots = Robots.concat(OpponentRobots);

	// convert mixed team info
	if (state.mixed_team_info && state.mixed_team_info.plans) {
		// MixedTeam = mixedTeam.decodeData(state.mixed_team_info.plans);
		MixedTeam = undefined;
	} else {
		MixedTeam = undefined;
	}

	// update aoi data
	AoI = state.tracking_aoi;

	// no vision data only if the parameter is false
	return state.has_vision_data !== false;
}

let gameStageMapping: {[name: string]: string} = {
	NORMAL_FIRST_HALF_PRE: "FirstHalfPre",
	NORMAL_FIRST_HALF: "FirstHalf",
	NORMAL_HALF_TIME: "HalfTime",
	NORMAL_SECOND_HALF_PRE: "SecondHalfPre",
	NORMAL_SECOND_HALF: "SecondHalf",

	EXTRA_TIME_BREAK: "ExtraTimeBreak",
	EXTRA_FIRST_HALF_PRE: "ExtraFirstHalfPre",
	EXTRA_FIRST_HALF: "ExtraFirstHalf",
	EXTRA_HALF_TIME: "ExtraHalfTime",
	EXTRA_SECOND_HALF_PRE: "ExtraSecondHalfPre",
	EXTRA_SECOND_HALF: "ExtraSecondHalf",

	PENALTY_SHOOTOUT_BREAK: "PenaltyShootoutBreak",
	PENALTY_SHOOTOUT: "PenaltyShootout",
	POST_GAME: "PostGame"
};

// keep for use by debugcommands.sendRefereeCommand
let fullRefereeState: pb.amun.GameState | undefined = undefined;

export function _getFullRefereeState() {
	return fullRefereeState;
}

// updates referee command and keeper information
function _updateGameState(state: pb.amun.GameState) {
	fullRefereeState = state;
	let refState = state.state;
	// map referee command to own team
	if (TeamIsBlue) {
		RefereeState = refState.replace("Blue", "Offensive").replace("Yellow", "Defensive");
	} else {
		RefereeState = refState.replace("Yellow", "Offensive").replace("Blue", "Defensive");
	}

	if (RefereeState === "TimeoutOffensive" || RefereeState === "TimeoutDefensive") {
		RefereeState = "Halt";
	}

	if (state.designated_position && state.designated_position.x != undefined) {
		BallPlacementPos = Coordinates.toLocal(Vector.createReadOnly(
			// refbox position message uses millimeters
			// ssl-vision's coordinate system is rotated by 90 degrees
			-state.designated_position.y / 1000,
			state.designated_position.x / 1000));
	}

	GameStage = gameStageMapping[state.stage];

	let friendlyTeamInfo = TeamIsBlue ? state.blue : state.yellow;
	let opponentTeamInfo = TeamIsBlue ? state.yellow : state.blue;

	let friendlyKeeperId = friendlyTeamInfo.goalie;
	let opponentKeeperId = opponentTeamInfo.goalie;

	let friendlyKeeper: FriendlyRobot | undefined = FriendlyRobotsById[friendlyKeeperId];
	if (friendlyKeeper && !friendlyKeeper.isVisible) {
		friendlyKeeper = undefined;
	}

	let opponentKeeper: Robot | undefined = OpponentRobotsById[opponentKeeperId];
	if (opponentKeeper && !opponentKeeper.isVisible) {
		opponentKeeper = undefined;
	}

	FriendlyKeeper = friendlyKeeper;
	OpponentKeeper = opponentKeeper;


// 	optional sint32 stage_time_left = 2;
// 	message TeamInfo {
// 		// The team's name (empty string if operator has not typed anything).
// 		required string name = 1;
// 		// The number of goals scored by the team during normal play and overtime.
// 		required uint32 score = 2;
// 		// The number of red cards issued to the team since the beginning of the game.
// 		required uint32 red_cards = 3;
// 		// The amount of time (in microseconds) left on each yellow card issued to the team.
// 		// If no yellow cards are issued, this array has no elements.
// 		// Otherwise, times are ordered from smallest to largest.
// 		repeated uint32 yellow_card_times = 4 [packed=true];
// 		// The total number of yellow cards ever issued to the team.
// 		required uint32 yellow_cards = 5;
// 		// The number of timeouts this team can still call.
// 		// If in a timeout right now, that timeout is excluded.
// 		required uint32 timeouts = 6;
// 		// The number of microseconds of timeout this team can use.
// 		required uint32 timeout_time = 7;
// 	}

	FriendlyYellowCards = [];
	if (friendlyTeamInfo.yellow_card_times != undefined) {
		for (let time of friendlyTeamInfo.yellow_card_times) {
			FriendlyYellowCards.push(time / 1000000);
		}
	}
	OpponentYellowCards = [];
	if (opponentTeamInfo.yellow_card_times != undefined) {
		for (let time of opponentTeamInfo.yellow_card_times) {
			OpponentYellowCards.push(time / 1000000);
		}
	}
	FriendlyRedCards = friendlyTeamInfo.red_cards;
	OpponentRedCards = opponentTeamInfo.red_cards;
}

/** update and handle user inputs set for own robots */
export function _updateUserInput(input: pb.amun.UserInput) {
	if (input.radio_command) {
		for (let robot of FriendlyRobotsAll) {
			robot._updateUserControl(undefined); // clear
		}
		for (let cmd of input.radio_command) {
			let robot = FriendlyRobotsById[cmd.id];
			if (robot) {
				robot._updateUserControl(cmd.command);
			}
		}
	}
	if (input.move_command) {
		// cache the movecommands for 0.3 seconds if it not there every frame
		for (let robot of FriendlyRobotsAll) {
			// < 0 for going back in logfiles while replaying
			if (robot.moveCommand && (Time - robot.moveCommand.time > 0.3  ||
					Time - robot.moveCommand.time < 0)) {
				robot.moveCommand = undefined;
			}
		}
		for (let cmd of input.move_command) {
			if (FriendlyRobotsById[cmd.id]) {
				FriendlyRobotsById[cmd.id].moveCommand = {time: Time, pos: Coordinates.toGlobal(new Vector(cmd.p_x || 0, cmd.p_y || 0))};
			} else {
				let teamColorString = TeamIsBlue ? "blue" : "yellow";
				amunLocal.log(`<font color="red">WARNING: </font>please select robot ${cmd.id} for team ${teamColorString} for pulling it`);
			}
		}
	}
}


/** Stops own robots and enables standby */
export function haltOwnRobots() {
	for (let robot of FriendlyRobotsAll) {
		if (robot.moveCommand == undefined) {
			robot.setStandby(true);
			robot.halt();
		}
	}
}

/**
 * Set generated commands for our robots.
 * Robots without a command stop by default
 */
export function setRobotCommands() {
	if (amunLocal.setCommands) {
		let commands: [number, number, any][] = [];
		for (let robot of FriendlyRobotsAll) {
			commands.push([robot.generation, robot.id, robot._command()]);
		}
		amunLocal.setCommands(commands);
	} else {
		for (let robot of FriendlyRobotsAll) {
			amunLocal.setCommand(robot.generation, robot.id, robot._command());
		}
	}
}

_init();

