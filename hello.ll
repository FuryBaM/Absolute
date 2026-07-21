; ModuleID = 'hello.abs'
source_filename = "hello.abs"
target triple = "x86_64-pc-windows-msvc"

%absolute.class.Error = type { ptr, ptr }

@absolute.vtable.Error = private constant [1 x ptr] [ptr @Error.__destroy]
@string.literal = private unnamed_addr constant [21 x i8] c"Hello from Absolute!\00", align 1
@null.text = private unnamed_addr constant [7 x i8] c"<null>\00", align 1
@print.format = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @Error.__ctor(ptr %this, ptr %value) {
entry:
  %value1 = alloca ptr, align 8
  store ptr %value, ptr %value1, align 8
  %message.address = getelementptr inbounds %absolute.class.Error, ptr %this, i32 0, i32 1
  %value.value = load ptr, ptr %value1, align 8
  store ptr %value.value, ptr %message.address, align 8
  ret void
}

define internal void @Error.__destroy(ptr %this) {
entry:
  ret void
}

define i32 @main() {
entry:
  %print.result = call i32 (ptr, ...) @printf(ptr @print.format, ptr @string.literal)
  ret i32 0
}

declare i32 @printf(ptr, ...)
