// NebulaCoop's built-in mods (galaxy::mods::install_builtin_mods): each one's id and luma.json, a copy of
// LumaLink-coop's builtin/<id>/luma.json, where they are made and checked (its "nebula-coop match" check compares the
// two). Change them there first, then copy each luma.json here between its R"luma( and )luma".
// clang-format off
{"character-movesets", R"luma(
{
  "authors": [
    "NebulaCoop"
  ],
  "description": "Each character moves their own way, as sm64coopdx's Character Movesets: Luigi gets Super Mario Galaxy's own, a higher and floatier jump, less grip and a long sliding brake, a stronger stomp bounce, his own Boo and a spin that costs air underwater. Off, everyone moves as Mario does.",
  "format": 1,
  "game": "RMG",
  "id": "character-movesets",
  "layers": [
    {
      "player_constants": {
        "luigi": {
          "mJumpHeight": [null, null, 34.71],
          "mGravityJumping": [1.6, 1.6, 1.6, 1.6, 1.6, 0.9, 1.6, 2.0, 0.35, null, 1.6, 0.4],
          "mTrampleLong": 6.0,
          "mWalkSpeed": 15.0,
          "mInertiaStandardStop": 0.99,
          "mInertiaStandardMax": 0.92,
          "mInertiaStop": 0.95,
          "mInertiaSquat": 0.97,
          "mInertiaTurnSlip": 0.98,
          "mInertiaTurning": 0.9,
          "mSlowStartTime": 20,
          "mTurnReadyTime": 20,
          "mInertiaBrake": 0.95,
          "mBrakeFirstTimer": 60,
          "mBrakeSecondTimer": 60,
          "mBrakeSlide": 1,
          "mSpinJumpHeight": 12.35,
          "mSpinJumpGravity": 0.45,
          "mRabbitMoveAcc2": 0.05,
          "mSwimSpinOxygen": 1,
          "mTeresaAngleDown": 0.75,
          "mTeresaWallReflectPower": 10.0,
          "mTeresaWallReflectReduction": 0.95,
          "mTeresaWallReflectTime": 30,
          "mTeresaAirWalkTurnSpd": 0.02,
          "mTeresaDropSpeedMax": 0.5,
          "mTeresaRiseSpeedMax": 0.5,
          "mTeresaDropBase": 12.0,
          "mTeresaUpInertia": 0.99,
          "mTeresaAccelTime": 10,
          "mTeresaTrgOnPushTime1": 5,
          "mTeresaOnePushAccel1": 0.05,
          "mTeresaOnePushAccel2": 0.01
        }
      }
    }
  ],
  "name": "Character Movesets",
  "requires": [
    "coop"
  ],
  "version": "1.0.0"
}
)luma"},
