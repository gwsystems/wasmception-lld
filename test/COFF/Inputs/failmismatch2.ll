; ModuleID = 'test2.cpp'
source_filename = "test2.cpp"
target datalayout = "e-m:w-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.16.27027"

; Function Attrs: noinline norecurse nounwind optnone sspstrong uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  store i32 0, i32* %1, align 4
  %2 = call i32 @"?f@@YAHXZ"()
  ret i32 %2
}

declare dso_local i32 @"?f@@YAHXZ"() #1

attributes #0 = { noinline norecurse nounwind optnone sspstrong uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.linker.options = !{!0, !1, !2}
!llvm.module.flags = !{!3, !4}
!llvm.ident = !{!5}

!0 = !{!"/DEFAULTLIB:libcmt.lib"}
!1 = !{!"/DEFAULTLIB:oldnames.lib"}
!2 = !{!"/FAILIFMISMATCH:\22TEST=2\22"}
!3 = !{i32 1, !"wchar_size", i32 2}
!4 = !{i32 7, !"PIC Level", i32 2}
!5 = !{!"clang version 7.0.1 (tags/RELEASE_701/final)"}
