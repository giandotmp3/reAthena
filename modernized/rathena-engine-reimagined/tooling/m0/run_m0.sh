#!/bin/sh
# usage: run_m0.sh ./m0host ./states   (from this directory)
H=${1:-./m0host}; S=${2:-./states}
echo "== M0(a) damage";   $H m0a_damage.luau 300000; $H --interrupt m0a_damage.luau 300000
echo "== M0(a) heavy";    $H m0a_heavy.luau 200000;  $H --interrupt m0a_heavy.luau 200000
echo "== M0(c) hooks";    $H m0c_hooks.luau 1000000; $H --interrupt m0c_hooks.luau 1000000
echo "== M0(c) sandbox";  $H --sandbox --memcap=67108864 m0c_sandbox.luau
echo "== M0(c) budget";   $H --sandbox --interrupt m0c_loop.luau
echo "== M0(c) native";   $H --sandbox --interrupt --memcap=268435456 m0c_native.luau
echo "== M0(c) states";   $S
echo "== M0(d) numeric";  $H m0d_numeric.luau
