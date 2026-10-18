#!/bin/sh
# Re-run every measurement. Output goes to out/*.txt. Needs: python3, the luau CLI path in $LUAU.
cd "$(dirname "$0")"
LUAU=${LUAU:-../luau-src/b/luau}
mkdir -p out
python3 -I prep.py > out/prep.txt 2>&1          # data/*.luau, manifest, builtins, consts (glue only)
./mk_nocount.sh
$LUAU t_split.luau            > out/split.txt 2>&1
$LUAU t_neg.luau              > out/parser_negative_tests.txt 2>&1
$LUAU t_semantics.luau        > out/semantics_tests.txt 2>&1
for s in re pre; do
  $LUAU run_parse.luau -a $s        > out/parse_$s.txt 2>&1
  $LUAU run_parse.luau -a $s lower  > out/parse_lower_$s.txt 2>&1
done
$LUAU run_usage.luau          > out/usage.txt 2>&1
$LUAU run_density.luau        > out/density.txt 2>&1
$LUAU run_constructs.luau     > out/constructs.txt 2>&1
$LUAU run_bench.luau          > out/bench.txt 2>&1
$LUAU --codegen run_bench.luau > out/bench_codegen.txt 2>&1
for f in npc/re/merchants/enchan_mal.txt npc/re/guild/invest_main.txt npc/re/quests/quests_16_1.txt npc/guild2/agit_main_se.txt npc/cities/prontera.txt \
         npc/quests/cooking_quest.txt npc/other/marriage.txt npc/re/instances/TempleOfDemonGod.txt npc/re/instances/FacewormsNest.txt npc/quests/quests_juperos.txt; do
  b=$(basename $f .txt)
  $LUAU run_worst.luau -a $f re 200 base 1500 > out/worst_${b}_base.txt 2>&1
  $LUAU run_worst.luau -a $f re 200 ext 1500  > out/worst_${b}_ext.txt 2>&1
done
for s in re pre; do for c in base ext; do $LUAU run_corpus_drive.luau -a $s $c > out/drive_${s}_$c.txt 2>&1; done; done
echo done
