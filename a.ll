; ModuleID = 'a.bc'
source_filename = "llvm-link"
target datalayout = "e-m:o-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx10.14.0"

; Function Attrs: noinline nounwind ssp uwtable
define void @foo(i32) #0 !dbg !8 {
  %r2 = alloca i32, align 4
  store i32 %0, i32* %r2, align 4
  call void @llvm.dbg.declare(metadata i32* %r2, metadata !12, metadata !DIExpression()), !dbg !13
  br label %Head, !dbg !14

Head:
  %r4 = load i32, i32* %r2, align 4, !dbg !15
  %r5 = icmp ne i32 %r4, 0, !dbg !15
  br i1 %r5, label %Cond1, label %Cond2, !dbg !16

Cond1:
  br label %Cond2

Cond2:
  %r8 = phi i1 [ false, %Head ], [ true, %Cond1 ], !dbg !17
  br i1 %r8, label %Body, label %Exit, !dbg !14

Body:
  call void @__CONTRACT_invariant(i1 zeroext true), !dbg !18
  br label %Head, !dbg !14, !llvm.loop !19

Exit:
  ret void, !dbg !20
}

; Function Attrs: nounwind readnone speculatable
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

declare void @__CONTRACT_invariant(i1 zeroext) #2

attributes #0 = { noinline nounwind ssp uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+cx16,+fxsr,+mmx,+sahf,+sse,+sse2,+sse3,+sse4.1,+ssse3,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readnone speculatable }
attributes #2 = { "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+cx16,+fxsr,+mmx,+sahf,+sse,+sse2,+sse3,+sse4.1,+ssse3,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.dbg.cu = !{!0}
!llvm.ident = !{!3}
!llvm.module.flags = !{!4, !5, !6, !7}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 8.0.1 (tags/RELEASE_801/final)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, nameTableKind: GNU)
!1 = !DIFile(filename: "a.c", directory: "/Users/emmmi/Code/smack")
!2 = !{}
!3 = !{!"clang version 8.0.1 (tags/RELEASE_801/final)"}
!4 = !{i32 2, !"Dwarf Version", i32 4}
!5 = !{i32 2, !"Debug Info Version", i32 3}
!6 = !{i32 1, !"wchar_size", i32 4}
!7 = !{i32 7, !"PIC Level", i32 2}
!8 = distinct !DISubprogram(name: "foo", scope: !1, file: !1, line: 4, type: !9, scopeLine: 4, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!9 = !DISubroutineType(types: !10)
!10 = !{null, !11}
!11 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!12 = !DILocalVariable(name: "i", arg: 1, scope: !8, file: !1, line: 4, type: !11)
!13 = !DILocation(line: 4, column: 14, scope: !8)
!14 = !DILocation(line: 5, column: 5, scope: !8)
!15 = !DILocation(line: 5, column: 12, scope: !8)
!16 = !DILocation(line: 5, column: 14, scope: !8)
!17 = !DILocation(line: 0, scope: !8)
!18 = !DILocation(line: 6, column: 9, scope: !8)
!19 = distinct !{!19, !14, !18}
!20 = !DILocation(line: 7, column: 1, scope: !8)
