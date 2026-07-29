; ModuleID = 'vector-sort-unsafe.abs'
source_filename = "vector-sort-unsafe.abs"
target triple = "x86_64-pc-windows-msvc"

%absolute.class.Error = type { ptr, ptr }
%"absolute.class.std.collections.VectorIterator<int32>" = type { ptr, %absolute.array.int32.1, i32, i32 }
%absolute.array.int32.1 = type { ptr, ptr, i64 }
%"absolute.class.std.collections.VectorBuilder<int32>" = type { ptr, %absolute.array.int32.1, i32, i32, i1 }
%"absolute.class.std.collections.Vector<int32>" = type { ptr, %absolute.array.int32.1, i32, i32 }

@Error.__vtable = internal constant [1 x ptr] [ptr @Error.__destroy], align 8
@"std.collections.VectorIterator<int32>.__vtable" = internal constant [1 x ptr] [ptr @"std.collections.VectorIterator<int32>.__destroy"], align 8
@"std.collections.VectorBuilder<int32>.__vtable" = internal constant [1 x ptr] [ptr @"std.collections.VectorBuilder<int32>.__destroy"], align 8
@"std.collections.Vector<int32>.__vtable" = internal constant [1 x ptr] [ptr @"std.collections.Vector<int32>.__destroy"], align 8
@print.format = private unnamed_addr constant [6 x i8] c"%lld\0A\00", align 1
@array.bounds.message = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.1 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal = private unnamed_addr constant [31 x i8] c"VectorBuilder already finished\00", align 1
@array.bounds.message.2 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.3 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.4 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.5 = private unnamed_addr constant [20 x i8] c"Index out of bounds\00", align 1
@array.bounds.message.6 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.7 = private unnamed_addr constant [31 x i8] c"VectorBuilder already finished\00", align 1
@string.literal.8 = private unnamed_addr constant [20 x i8] c"Index out of bounds\00", align 1
@array.bounds.message.9 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.10 = private unnamed_addr constant [31 x i8] c"VectorBuilder already finished\00", align 1
@string.literal.11 = private unnamed_addr constant [20 x i8] c"Index out of bounds\00", align 1
@array.bounds.message.12 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.13 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.14 = private unnamed_addr constant [31 x i8] c"VectorBuilder already finished\00", align 1
@array.bounds.message.15 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.16 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.17 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.18 = private unnamed_addr constant [16 x i8] c"Vector is empty\00", align 1
@array.bounds.message.19 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.20 = private unnamed_addr constant [16 x i8] c"Vector is empty\00", align 1
@array.bounds.message.21 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.22 = private unnamed_addr constant [16 x i8] c"Vector is empty\00", align 1
@array.bounds.message.23 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.24 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.25 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.26 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.27 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.28 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.29 = private unnamed_addr constant [20 x i8] c"Index out of bounds\00", align 1
@array.bounds.message.30 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.31 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.32 = private unnamed_addr constant [20 x i8] c"Index out of bounds\00", align 1
@null.text = private unnamed_addr constant [7 x i8] c"<null>\00", align 1
@print.format.33 = private unnamed_addr constant [22 x i8] c"Assertion failed: %s\0A\00", align 1
@string.literal.34 = private unnamed_addr constant [20 x i8] c"Index out of bounds\00", align 1
@null.text.35 = private unnamed_addr constant [7 x i8] c"<null>\00", align 1
@print.format.36 = private unnamed_addr constant [22 x i8] c"Assertion failed: %s\0A\00", align 1
@array.bounds.message.37 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.38 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1

define void @"Error.__ctor$string"(ptr %this, ptr %value) {
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

define i1 @"std.collections.VectorIterator<int32>.next"(ptr %this) {
entry:
  %index.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 3
  %index.address1 = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 3
  %index.value = load i32, ptr %index.address1, align 4
  %add = add i32 %index.value, 1
  store i32 %add, ptr %index.address, align 4
  %index.address2 = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 3
  %index.value3 = load i32, ptr %index.address2, align 4
  %length.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 2
  %length.value = load i32, ptr %length.address, align 4
  %less = icmp slt i32 %index.value3, %length.value
  ret i1 %less
}

define i32 @"std.collections.VectorIterator<int32>.__absolute_property_get_value"(ptr %this) {
entry:
  %snapshot.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 1
  %snapshot.value = load %absolute.array.int32.1, ptr %snapshot.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %snapshot.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %snapshot.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %snapshot.value, 2
  %snapshot.address1 = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 1
  %snapshot.value2 = load %absolute.array.int32.1, ptr %snapshot.address1, align 8
  %array.data3 = extractvalue %absolute.array.int32.1 %snapshot.value2, 0
  %array.owner4 = extractvalue %absolute.array.int32.1 %snapshot.value2, 1
  %array.dimension5 = extractvalue %absolute.array.int32.1 %snapshot.value2, 2
  %index.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 3
  %index.value = load i32, ptr %index.address, align 4
  %array.index.wide = sext i32 %index.value to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension5
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

array.bounds.success:                             ; preds = %entry
  %array.element.address = getelementptr inbounds i32, ptr %array.data3, i32 %index.value
  %array.element = load i32, ptr %array.element.address, align 4
  ret i32 %array.element

array.bounds.failure:                             ; preds = %entry
  %0 = call i32 @puts(ptr @array.bounds.message)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.VectorIterator<int32>.__ctor$int32_5B_5D$int32"(ptr %this, %absolute.array.int32.1 %source, i1 %source.is_owner, i32 %sourceLength) {
entry:
  %sourceLength2 = alloca i32, align 4
  %source.is_owner1 = alloca i1, align 1
  %source.array.owner = alloca ptr, align 8
  %array.data = extractvalue %absolute.array.int32.1 %source, 0
  %array.owner = extractvalue %absolute.array.int32.1 %source, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %source, 2
  store ptr %array.owner, ptr %source.array.owner, align 8
  store i1 %source.is_owner, ptr %source.is_owner1, align 1
  store i32 %sourceLength, ptr %sourceLength2, align 4
  %snapshot.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 1
  %source.array.owner3 = load ptr, ptr %source.array.owner, align 8
  %sourceLength.value = load i32, ptr %sourceLength2, align 4
  %int.cast = sext i32 %sourceLength.value to i64
  %slice.order.valid = icmp sge i64 %int.cast, 0
  %slice.lower.valid = and i1 true, %slice.order.valid
  %slice.end.below.size = icmp sle i64 %int.cast, %array.dimension
  %slice.valid = and i1 %slice.lower.valid, %slice.end.below.size
  br i1 %slice.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

array.bounds.success:                             ; preds = %entry
  %slice.dim.length = sub i64 %int.cast, 0
  %slice.data = getelementptr inbounds i32, ptr %array.data, i64 0
  %array.data4 = insertvalue %absolute.array.int32.1 undef, ptr %slice.data, 0
  %array.owner5 = insertvalue %absolute.array.int32.1 %array.data4, ptr %source.array.owner3, 1
  %array.dimension6 = insertvalue %absolute.array.int32.1 %array.owner5, i64 %slice.dim.length, 2
  %array.data7 = extractvalue %absolute.array.int32.1 %array.dimension6, 0
  %array.owner8 = extractvalue %absolute.array.int32.1 %array.dimension6, 1
  %array.dimension9 = extractvalue %absolute.array.int32.1 %array.dimension6, 2
  %copy.element.count = mul i64 1, %array.dimension9
  %copy.byte.count = mul i64 %copy.element.count, 4
  %copy.data = call ptr @malloc(i64 %copy.byte.count)
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %copy.data, ptr align 1 %array.data7, i64 %copy.byte.count, i1 false)
  %array.data10 = insertvalue %absolute.array.int32.1 undef, ptr %copy.data, 0
  %array.owner11 = insertvalue %absolute.array.int32.1 %array.data10, ptr %copy.data, 1
  %array.dimension12 = insertvalue %absolute.array.int32.1 %array.owner11, i64 %array.dimension9, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %snapshot.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %snapshot.address, align 8
  store %absolute.array.int32.1 %array.dimension12, ptr %snapshot.address, align 8
  %length.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 2
  %sourceLength.value13 = load i32, ptr %sourceLength2, align 4
  store i32 %sourceLength.value13, ptr %length.address, align 4
  %index.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 3
  store i32 -1, ptr %index.address, align 4
  %role.is.owner = load i1, ptr %source.is_owner1, align 1
  br i1 %role.is.owner, label %role.owner.cleanup, label %role.cleanup.end

array.bounds.failure:                             ; preds = %entry
  %0 = call i32 @puts(ptr @array.bounds.message.1)
  call void @exit(i32 1)
  unreachable

role.owner.cleanup:                               ; preds = %array.bounds.success
  %cleanup.array.owner = load ptr, ptr %source.array.owner, align 8
  call void @free(ptr %cleanup.array.owner)
  br label %role.cleanup.end

role.cleanup.end:                                 ; preds = %role.owner.cleanup, %array.bounds.success
  ret void
}

define internal void @"std.collections.VectorIterator<int32>.__destroy"(ptr %this) {
entry:
  %snapshot.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 1
  %field.cleanup.array = load %absolute.array.int32.1, ptr %snapshot.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %snapshot.address, align 8
  ret void
}

define i32 @"std.collections.VectorBuilder<int32>.__absolute_property_get_count"(ptr %this) {
entry:
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  ret i32 %_count.value
}

define void @"std.collections.VectorBuilder<int32>.add$int32"(ptr %this, i32 %element) {
entry:
  %i = alloca i32, align 4
  %newBuf.array.owner = alloca ptr, align 8
  %newCap = alloca i32, align 4
  %element1 = alloca i32, align 4
  store i32 %element, ptr %element1, align 4
  %_finished.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 4
  %_finished.value = load i1, ptr %_finished.address, align 1
  br i1 %_finished.value, label %if.body, label %if.end

if.end:                                           ; preds = %entry
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 3
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %equal = icmp eq i32 %_count.value, %_capacity.value
  br i1 %equal, label %if.body3, label %if.end2

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.pointee = call ptr @absolute_managed_require(i64 %managed.handle)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.pointee, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.pointee, ptr @string.literal)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %if.body
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret void

error.continue:                                   ; preds = %if.body
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %0 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %0, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret void

if.end2:                                          ; preds = %while.end, %if.end
  %buffer.address39 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value40 = load %absolute.array.int32.1, ptr %buffer.address39, align 8
  %array.data41 = extractvalue %absolute.array.int32.1 %buffer.value40, 0
  %array.owner42 = extractvalue %absolute.array.int32.1 %buffer.value40, 1
  %array.dimension43 = extractvalue %absolute.array.int32.1 %buffer.value40, 2
  %buffer.address44 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value45 = load %absolute.array.int32.1, ptr %buffer.address44, align 8
  %array.data46 = extractvalue %absolute.array.int32.1 %buffer.value45, 0
  %array.owner47 = extractvalue %absolute.array.int32.1 %buffer.value45, 1
  %array.dimension48 = extractvalue %absolute.array.int32.1 %buffer.value45, 2
  %_count.address49 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value50 = load i32, ptr %_count.address49, align 4
  %array.index.wide51 = sext i32 %_count.value50 to i64
  %array.index.valid52 = icmp ult i64 %array.index.wide51, %array.dimension48
  br i1 %array.index.valid52, label %array.bounds.success53, label %array.bounds.failure54, !prof !0

if.body3:                                         ; preds = %if.end
  %_capacity.address4 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 3
  %_capacity.value5 = load i32, ptr %_capacity.address4, align 4
  %mul = mul i32 %_capacity.value5, 2
  store i32 %mul, ptr %newCap, align 4
  %newCap.value = load i32, ptr %newCap, align 4
  %int.cast = sext i32 %newCap.value to i64
  %array.alloc.bytes = mul i64 %int.cast, 4
  %array.data.alloc = call ptr @malloc(i64 %array.alloc.bytes)
  %array.data = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc, 0
  %array.owner = insertvalue %absolute.array.int32.1 %array.data, ptr %array.data.alloc, 1
  %array.dimension = insertvalue %absolute.array.int32.1 %array.owner, i64 %int.cast, 2
  %array.data6 = extractvalue %absolute.array.int32.1 %array.dimension, 0
  %array.owner7 = extractvalue %absolute.array.int32.1 %array.dimension, 1
  %array.dimension8 = extractvalue %absolute.array.int32.1 %array.dimension, 2
  store ptr %array.owner7, ptr %newBuf.array.owner, align 8
  %array.data9 = insertvalue %absolute.array.int32.1 undef, ptr %array.data6, 0
  %array.owner10 = insertvalue %absolute.array.int32.1 %array.data9, ptr %array.owner7, 1
  %array.dimension11 = insertvalue %absolute.array.int32.1 %array.owner10, i64 %array.dimension8, 2
  store i32 0, ptr %i, align 4
  br label %while.condition

while.condition:                                  ; preds = %array.bounds.success28, %if.body3
  %i.value = load i32, ptr %i, align 4
  %_count.address12 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value13 = load i32, ptr %_count.address12, align 4
  %less = icmp slt i32 %i.value, %_count.value13
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %newBuf.array.owner14 = load ptr, ptr %newBuf.array.owner, align 8
  %newBuf.array.owner15 = load ptr, ptr %newBuf.array.owner, align 8
  %i.value16 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value16 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension8
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %buffer.address32 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %newBuf.array.owner33 = load ptr, ptr %newBuf.array.owner, align 8
  %array.data34 = insertvalue %absolute.array.int32.1 undef, ptr %array.data6, 0
  %array.owner35 = insertvalue %absolute.array.int32.1 %array.data34, ptr %newBuf.array.owner33, 1
  %array.dimension36 = insertvalue %absolute.array.int32.1 %array.owner35, i64 %array.dimension8, 2
  store ptr null, ptr %newBuf.array.owner, align 8
  %field.cleanup.array = load %absolute.array.int32.1, ptr %buffer.address32, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %buffer.address32, align 8
  store %absolute.array.int32.1 %array.dimension36, ptr %buffer.address32, align 8
  %_capacity.address37 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 3
  %newCap.value38 = load i32, ptr %newCap, align 4
  store i32 %newCap.value38, ptr %_capacity.address37, align 4
  br label %if.end2

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data6, i32 %i.value16
  %buffer.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value = load %absolute.array.int32.1, ptr %buffer.address, align 8
  %array.data17 = extractvalue %absolute.array.int32.1 %buffer.value, 0
  %array.owner18 = extractvalue %absolute.array.int32.1 %buffer.value, 1
  %array.dimension19 = extractvalue %absolute.array.int32.1 %buffer.value, 2
  %buffer.address20 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value21 = load %absolute.array.int32.1, ptr %buffer.address20, align 8
  %array.data22 = extractvalue %absolute.array.int32.1 %buffer.value21, 0
  %array.owner23 = extractvalue %absolute.array.int32.1 %buffer.value21, 1
  %array.dimension24 = extractvalue %absolute.array.int32.1 %buffer.value21, 2
  %i.value25 = load i32, ptr %i, align 4
  %array.index.wide26 = sext i32 %i.value25 to i64
  %array.index.valid27 = icmp ult i64 %array.index.wide26, %array.dimension24
  br i1 %array.index.valid27, label %array.bounds.success28, label %array.bounds.failure29, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %1 = call i32 @puts(ptr @array.bounds.message.2)
  call void @exit(i32 1)
  unreachable

array.bounds.success28:                           ; preds = %array.bounds.success
  %array.element.address30 = getelementptr inbounds i32, ptr %array.data22, i32 %i.value25
  %array.element = load i32, ptr %array.element.address30, align 4
  store i32 %array.element, ptr %array.element.address, align 4
  %i.value31 = load i32, ptr %i, align 4
  %add = add i32 %i.value31, 1
  store i32 %add, ptr %i, align 4
  br label %while.condition

array.bounds.failure29:                           ; preds = %array.bounds.success
  %2 = call i32 @puts(ptr @array.bounds.message.3)
  call void @exit(i32 1)
  unreachable

array.bounds.success53:                           ; preds = %if.end2
  %array.element.address55 = getelementptr inbounds i32, ptr %array.data46, i32 %_count.value50
  %move.value = load i32, ptr %element1, align 4
  call void @llvm.memset.p0.i64(ptr align 8 %element1, i8 0, i64 4, i1 false)
  store i32 %move.value, ptr %array.element.address55, align 4
  %_count.address56 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.address57 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value58 = load i32, ptr %_count.address57, align 4
  %add59 = add i32 %_count.value58, 1
  store i32 %add59, ptr %_count.address56, align 4
  ret void

array.bounds.failure54:                           ; preds = %if.end2
  %3 = call i32 @puts(ptr @array.bounds.message.4)
  call void @exit(i32 1)
  unreachable
}

define i32 @"std.collections.VectorBuilder<int32>.getAt$int32"(ptr %this, i32 %index) {
entry:
  %index1 = alloca i32, align 4
  store i32 %index, ptr %index1, align 4
  %index.value = load i32, ptr %index1, align 4
  %less = icmp slt i32 %index.value, 0
  %index.value2 = load i32, ptr %index1, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %greater.equal = icmp sge i32 %index.value2, %_count.value
  %logical.or = or i1 %less, %greater.equal
  br i1 %logical.or, label %if.body, label %if.end

if.end:                                           ; preds = %entry
  %buffer.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value = load %absolute.array.int32.1, ptr %buffer.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %buffer.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %buffer.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %buffer.value, 2
  %buffer.address3 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value4 = load %absolute.array.int32.1, ptr %buffer.address3, align 8
  %array.data5 = extractvalue %absolute.array.int32.1 %buffer.value4, 0
  %array.owner6 = extractvalue %absolute.array.int32.1 %buffer.value4, 1
  %array.dimension7 = extractvalue %absolute.array.int32.1 %buffer.value4, 2
  %index.value8 = load i32, ptr %index1, align 4
  %array.index.wide = sext i32 %index.value8 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension7
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.pointee = call ptr @absolute_managed_require(i64 %managed.handle)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.pointee, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.pointee, ptr @string.literal.5)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %if.body
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i32 0

error.continue:                                   ; preds = %if.body
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %0 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %0, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret i32 0

array.bounds.success:                             ; preds = %if.end
  %array.element.address = getelementptr inbounds i32, ptr %array.data5, i32 %index.value8
  %array.element = load i32, ptr %array.element.address, align 4
  ret i32 %array.element

array.bounds.failure:                             ; preds = %if.end
  %1 = call i32 @puts(ptr @array.bounds.message.6)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.VectorBuilder<int32>.setAt$int32$int32"(ptr %this, i32 %index, i32 %element) {
entry:
  %element2 = alloca i32, align 4
  %index1 = alloca i32, align 4
  store i32 %index, ptr %index1, align 4
  store i32 %element, ptr %element2, align 4
  %_finished.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 4
  %_finished.value = load i1, ptr %_finished.address, align 1
  br i1 %_finished.value, label %if.body, label %if.end

if.end:                                           ; preds = %entry
  %index.value = load i32, ptr %index1, align 4
  %less = icmp slt i32 %index.value, 0
  %index.value4 = load i32, ptr %index1, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %greater.equal = icmp sge i32 %index.value4, %_count.value
  %logical.or = or i1 %less, %greater.equal
  br i1 %logical.or, label %if.body5, label %if.end3

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.pointee = call ptr @absolute_managed_require(i64 %managed.handle)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.pointee, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.pointee, ptr @string.literal.7)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %if.body
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret void

error.continue:                                   ; preds = %if.body
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %0 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %0, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret void

if.end3:                                          ; preds = %if.end
  %buffer.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value = load %absolute.array.int32.1, ptr %buffer.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %buffer.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %buffer.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %buffer.value, 2
  %buffer.address14 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value15 = load %absolute.array.int32.1, ptr %buffer.address14, align 8
  %array.data16 = extractvalue %absolute.array.int32.1 %buffer.value15, 0
  %array.owner17 = extractvalue %absolute.array.int32.1 %buffer.value15, 1
  %array.dimension18 = extractvalue %absolute.array.int32.1 %buffer.value15, 2
  %index.value19 = load i32, ptr %index1, align 4
  %array.index.wide = sext i32 %index.value19 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension18
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

if.body5:                                         ; preds = %if.end
  %managed.handle6 = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle6, i64 6860172195720241041)
  %managed.pointee7 = call ptr @absolute_managed_require(i64 %managed.handle6)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee7, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address8 = getelementptr inbounds %absolute.class.Error, ptr %managed.pointee7, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address8, align 8
  call void @"Error.__ctor$string"(ptr %managed.pointee7, ptr @string.literal.8)
  %error.pending9 = call i1 @absolute_error_pending()
  br i1 %error.pending9, label %error.propagate10, label %error.continue11

error.propagate10:                                ; preds = %if.body5
  call void @absolute_managed_destroy(i64 %managed.handle6)
  ret void

error.continue11:                                 ; preds = %if.body5
  %exception.dynamic.type12 = call i64 @absolute_managed_type(i64 %managed.handle6)
  %1 = icmp ne i64 %exception.dynamic.type12, 0
  %exception.effective.type13 = select i1 %1, i64 %exception.dynamic.type12, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle6, i64 %exception.effective.type13)
  ret void

array.bounds.success:                             ; preds = %if.end3
  %array.element.address = getelementptr inbounds i32, ptr %array.data16, i32 %index.value19
  %move.value = load i32, ptr %element2, align 4
  call void @llvm.memset.p0.i64(ptr align 8 %element2, i8 0, i64 4, i1 false)
  store i32 %move.value, ptr %array.element.address, align 4
  ret void

array.bounds.failure:                             ; preds = %if.end3
  %2 = call i32 @puts(ptr @array.bounds.message.9)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.VectorBuilder<int32>.removeAt$int32"(ptr %this, i32 %index) {
entry:
  %i = alloca i32, align 4
  %index1 = alloca i32, align 4
  store i32 %index, ptr %index1, align 4
  %_finished.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 4
  %_finished.value = load i1, ptr %_finished.address, align 1
  br i1 %_finished.value, label %if.body, label %if.end

if.end:                                           ; preds = %entry
  %index.value = load i32, ptr %index1, align 4
  %less = icmp slt i32 %index.value, 0
  %index.value3 = load i32, ptr %index1, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %greater.equal = icmp sge i32 %index.value3, %_count.value
  %logical.or = or i1 %less, %greater.equal
  br i1 %logical.or, label %if.body4, label %if.end2

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.pointee = call ptr @absolute_managed_require(i64 %managed.handle)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.pointee, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.pointee, ptr @string.literal.10)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %if.body
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret void

error.continue:                                   ; preds = %if.body
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %0 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %0, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret void

if.end2:                                          ; preds = %if.end
  %index.value13 = load i32, ptr %index1, align 4
  store i32 %index.value13, ptr %i, align 4
  br label %while.condition

if.body4:                                         ; preds = %if.end
  %managed.handle5 = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle5, i64 6860172195720241041)
  %managed.pointee6 = call ptr @absolute_managed_require(i64 %managed.handle5)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee6, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address7 = getelementptr inbounds %absolute.class.Error, ptr %managed.pointee6, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address7, align 8
  call void @"Error.__ctor$string"(ptr %managed.pointee6, ptr @string.literal.11)
  %error.pending8 = call i1 @absolute_error_pending()
  br i1 %error.pending8, label %error.propagate9, label %error.continue10

error.propagate9:                                 ; preds = %if.body4
  call void @absolute_managed_destroy(i64 %managed.handle5)
  ret void

error.continue10:                                 ; preds = %if.body4
  %exception.dynamic.type11 = call i64 @absolute_managed_type(i64 %managed.handle5)
  %1 = icmp ne i64 %exception.dynamic.type11, 0
  %exception.effective.type12 = select i1 %1, i64 %exception.dynamic.type11, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle5, i64 %exception.effective.type12)
  ret void

while.condition:                                  ; preds = %array.bounds.success36, %if.end2
  %i.value = load i32, ptr %i, align 4
  %_count.address14 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value15 = load i32, ptr %_count.address14, align 4
  %sub = sub i32 %_count.value15, 1
  %less16 = icmp slt i32 %i.value, %sub
  br i1 %less16, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %buffer.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value = load %absolute.array.int32.1, ptr %buffer.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %buffer.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %buffer.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %buffer.value, 2
  %buffer.address17 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value18 = load %absolute.array.int32.1, ptr %buffer.address17, align 8
  %array.data19 = extractvalue %absolute.array.int32.1 %buffer.value18, 0
  %array.owner20 = extractvalue %absolute.array.int32.1 %buffer.value18, 1
  %array.dimension21 = extractvalue %absolute.array.int32.1 %buffer.value18, 2
  %i.value22 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value22 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension21
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %_count.address41 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.address42 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value43 = load i32, ptr %_count.address42, align 4
  %sub44 = sub i32 %_count.value43, 1
  store i32 %sub44, ptr %_count.address41, align 4
  ret void

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data19, i32 %i.value22
  %buffer.address23 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value24 = load %absolute.array.int32.1, ptr %buffer.address23, align 8
  %array.data25 = extractvalue %absolute.array.int32.1 %buffer.value24, 0
  %array.owner26 = extractvalue %absolute.array.int32.1 %buffer.value24, 1
  %array.dimension27 = extractvalue %absolute.array.int32.1 %buffer.value24, 2
  %buffer.address28 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value29 = load %absolute.array.int32.1, ptr %buffer.address28, align 8
  %array.data30 = extractvalue %absolute.array.int32.1 %buffer.value29, 0
  %array.owner31 = extractvalue %absolute.array.int32.1 %buffer.value29, 1
  %array.dimension32 = extractvalue %absolute.array.int32.1 %buffer.value29, 2
  %i.value33 = load i32, ptr %i, align 4
  %add = add i32 %i.value33, 1
  %array.index.wide34 = sext i32 %add to i64
  %array.index.valid35 = icmp ult i64 %array.index.wide34, %array.dimension32
  br i1 %array.index.valid35, label %array.bounds.success36, label %array.bounds.failure37, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %2 = call i32 @puts(ptr @array.bounds.message.12)
  call void @exit(i32 1)
  unreachable

array.bounds.success36:                           ; preds = %array.bounds.success
  %array.element.address38 = getelementptr inbounds i32, ptr %array.data30, i32 %add
  %array.element = load i32, ptr %array.element.address38, align 4
  store i32 %array.element, ptr %array.element.address, align 4
  %i.value39 = load i32, ptr %i, align 4
  %add40 = add i32 %i.value39, 1
  store i32 %add40, ptr %i, align 4
  br label %while.condition

array.bounds.failure37:                           ; preds = %array.bounds.success
  %3 = call i32 @puts(ptr @array.bounds.message.13)
  call void @exit(i32 1)
  unreachable
}

define i64 @"std.collections.VectorBuilder<int32>.finish"(ptr %this) {
entry:
  %i = alloca i32, align 4
  %result.cached.pointee = alloca ptr, align 8
  %result = alloca i64, align 8
  %_finished.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 4
  %_finished.value = load i1, ptr %_finished.address, align 1
  br i1 %_finished.value, label %if.body, label %if.end

if.end:                                           ; preds = %entry
  %_finished.address1 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 4
  store i1 true, ptr %_finished.address1, align 1
  %managed.handle2 = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.Vector<int32>", ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle2, i64 -6152174195361087120)
  %managed.pointee3 = call ptr @absolute_managed_require(i64 %managed.handle2)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee3, i8 0, i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.Vector<int32>", ptr null, i32 1) to i64), i1 false)
  %vtable.address4 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %managed.pointee3, i32 0, i32 0
  store ptr @"std.collections.Vector<int32>.__vtable", ptr %vtable.address4, align 8
  call void @"std.collections.Vector<int32>.__ctor"(ptr %managed.pointee3)
  %error.pending5 = call i1 @absolute_error_pending()
  br i1 %error.pending5, label %error.propagate6, label %error.continue7

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.pointee = call ptr @absolute_managed_require(i64 %managed.handle)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.pointee, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.pointee, ptr @string.literal.14)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %if.body
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i64 0

error.continue:                                   ; preds = %if.body
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %0 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %0, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret i64 0

error.propagate6:                                 ; preds = %if.end
  call void @absolute_managed_destroy(i64 %managed.handle2)
  %cleanup.handle = load i64, ptr %result, align 4
  %managed.pointee8 = call ptr @absolute_managed_get(i64 %cleanup.handle)
  %aggregate.present = icmp ne ptr %managed.pointee8, null
  br i1 %aggregate.present, label %aggregate.cleanup, label %aggregate.cleanup.end

error.continue7:                                  ; preds = %if.end
  store i64 %managed.handle2, ptr %result, align 4
  store ptr %managed.pointee3, ptr %result.cached.pointee, align 8
  store i32 0, ptr %i, align 4
  br label %while.condition

aggregate.cleanup:                                ; preds = %error.propagate6
  %aggregate.vtable = load ptr, ptr %managed.pointee8, align 8
  %aggregate.destroy.slot = getelementptr ptr, ptr %aggregate.vtable, i64 0
  %aggregate.destroy = load ptr, ptr %aggregate.destroy.slot, align 8
  call void %aggregate.destroy(ptr %managed.pointee8)
  br label %aggregate.cleanup.end

aggregate.cleanup.end:                            ; preds = %aggregate.cleanup, %error.propagate6
  call void @absolute_managed_destroy(i64 %cleanup.handle)
  store i64 0, ptr %result, align 4
  ret i64 0

while.condition:                                  ; preds = %array.bounds.success, %error.continue7
  %i.value = load i32, ptr %i, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %less = icmp slt i32 %i.value, %_count.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %result.value = load i64, ptr %result, align 4
  %result.cached.pointee9 = load ptr, ptr %result.cached.pointee, align 8
  %buffer.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value = load %absolute.array.int32.1, ptr %buffer.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %buffer.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %buffer.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %buffer.value, 2
  %buffer.address10 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value11 = load %absolute.array.int32.1, ptr %buffer.address10, align 8
  %array.data12 = extractvalue %absolute.array.int32.1 %buffer.value11, 0
  %array.owner13 = extractvalue %absolute.array.int32.1 %buffer.value11, 1
  %array.dimension14 = extractvalue %absolute.array.int32.1 %buffer.value11, 2
  %i.value15 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value15 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension14
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %result.value17 = load i64, ptr %result, align 4
  ret i64 %result.value17

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data12, i32 %i.value15
  %array.element = load i32, ptr %array.element.address, align 4
  call void @"std.collections.Vector<int32>.push$int32"(ptr %result.cached.pointee9, i32 %array.element)
  %i.value16 = load i32, ptr %i, align 4
  %add = add i32 %i.value16, 1
  store i32 %add, ptr %i, align 4
  br label %while.condition

array.bounds.failure:                             ; preds = %while.body
  %1 = call i32 @puts(ptr @array.bounds.message.15)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.VectorBuilder<int32>.__ctor$int32_5B_5D$int32"(ptr %this, %absolute.array.int32.1 %initialSource, i1 %initialSource.is_owner, i32 %initialCount) {
entry:
  %i = alloca i32, align 4
  %initialCount2 = alloca i32, align 4
  %initialSource.is_owner1 = alloca i1, align 1
  %initialSource.array.owner = alloca ptr, align 8
  %array.data = extractvalue %absolute.array.int32.1 %initialSource, 0
  %array.owner = extractvalue %absolute.array.int32.1 %initialSource, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %initialSource, 2
  store ptr %array.owner, ptr %initialSource.array.owner, align 8
  store i1 %initialSource.is_owner, ptr %initialSource.is_owner1, align 1
  store i32 %initialCount, ptr %initialCount2, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %initialCount.value = load i32, ptr %initialCount2, align 4
  store i32 %initialCount.value, ptr %_count.address, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 3
  %initialCount.value3 = load i32, ptr %initialCount2, align 4
  %greater = icmp sgt i32 %initialCount.value3, 4
  br i1 %greater, label %ternary.true, label %ternary.false

ternary.true:                                     ; preds = %entry
  %initialCount.value4 = load i32, ptr %initialCount2, align 4
  br label %ternary.end

ternary.false:                                    ; preds = %entry
  br label %ternary.end

ternary.end:                                      ; preds = %ternary.false, %ternary.true
  %ternary.result = phi i32 [ %initialCount.value4, %ternary.true ], [ 4, %ternary.false ]
  store i32 %ternary.result, ptr %_capacity.address, align 4
  %buffer.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %_capacity.address5 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 3
  %_capacity.value = load i32, ptr %_capacity.address5, align 4
  %int.cast = sext i32 %_capacity.value to i64
  %array.alloc.bytes = mul i64 %int.cast, 4
  %array.data.alloc = call ptr @malloc(i64 %array.alloc.bytes)
  %array.data6 = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc, 0
  %array.owner7 = insertvalue %absolute.array.int32.1 %array.data6, ptr %array.data.alloc, 1
  %array.dimension8 = insertvalue %absolute.array.int32.1 %array.owner7, i64 %int.cast, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %buffer.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %buffer.address, align 8
  store %absolute.array.int32.1 %array.dimension8, ptr %buffer.address, align 8
  store i32 0, ptr %i, align 4
  br label %while.condition

while.condition:                                  ; preds = %array.bounds.success25, %ternary.end
  %i.value = load i32, ptr %i, align 4
  %_count.address9 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address9, align 4
  %less = icmp slt i32 %i.value, %_count.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %buffer.address10 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value = load %absolute.array.int32.1, ptr %buffer.address10, align 8
  %array.data11 = extractvalue %absolute.array.int32.1 %buffer.value, 0
  %array.owner12 = extractvalue %absolute.array.int32.1 %buffer.value, 1
  %array.dimension13 = extractvalue %absolute.array.int32.1 %buffer.value, 2
  %buffer.address14 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value15 = load %absolute.array.int32.1, ptr %buffer.address14, align 8
  %array.data16 = extractvalue %absolute.array.int32.1 %buffer.value15, 0
  %array.owner17 = extractvalue %absolute.array.int32.1 %buffer.value15, 1
  %array.dimension18 = extractvalue %absolute.array.int32.1 %buffer.value15, 2
  %i.value19 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value19 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension18
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %_finished.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 4
  store i1 false, ptr %_finished.address, align 1
  %role.is.owner = load i1, ptr %initialSource.is_owner1, align 1
  br i1 %role.is.owner, label %role.owner.cleanup, label %role.cleanup.end

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data16, i32 %i.value19
  %initialSource.array.owner20 = load ptr, ptr %initialSource.array.owner, align 8
  %initialSource.array.owner21 = load ptr, ptr %initialSource.array.owner, align 8
  %i.value22 = load i32, ptr %i, align 4
  %array.index.wide23 = sext i32 %i.value22 to i64
  %array.index.valid24 = icmp ult i64 %array.index.wide23, %array.dimension
  br i1 %array.index.valid24, label %array.bounds.success25, label %array.bounds.failure26, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.16)
  call void @exit(i32 1)
  unreachable

array.bounds.success25:                           ; preds = %array.bounds.success
  %array.element.address27 = getelementptr inbounds i32, ptr %array.data, i32 %i.value22
  %array.element = load i32, ptr %array.element.address27, align 4
  store i32 %array.element, ptr %array.element.address, align 4
  %i.value28 = load i32, ptr %i, align 4
  %add = add i32 %i.value28, 1
  store i32 %add, ptr %i, align 4
  br label %while.condition

array.bounds.failure26:                           ; preds = %array.bounds.success
  %1 = call i32 @puts(ptr @array.bounds.message.17)
  call void @exit(i32 1)
  unreachable

role.owner.cleanup:                               ; preds = %while.end
  %cleanup.array.owner = load ptr, ptr %initialSource.array.owner, align 8
  call void @free(ptr %cleanup.array.owner)
  br label %role.cleanup.end

role.cleanup.end:                                 ; preds = %role.owner.cleanup, %while.end
  ret void
}

define internal void @"std.collections.VectorBuilder<int32>.__destroy"(ptr %this) {
entry:
  %buffer.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %field.cleanup.array = load %absolute.array.int32.1, ptr %buffer.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %buffer.address, align 8
  ret void
}

define i32 @"std.collections.Vector<int32>.__absolute_property_get_count"(ptr %this) {
entry:
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  ret i32 %_count.value
}

define i32 @"std.collections.Vector<int32>.__absolute_property_get_last"(ptr %this) {
entry:
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %equal = icmp eq i32 %_count.value, 0
  br i1 %equal, label %if.body, label %if.end

if.end:                                           ; preds = %entry
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %items.value, 2
  %items.address1 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value2 = load %absolute.array.int32.1, ptr %items.address1, align 8
  %array.data3 = extractvalue %absolute.array.int32.1 %items.value2, 0
  %array.owner4 = extractvalue %absolute.array.int32.1 %items.value2, 1
  %array.dimension5 = extractvalue %absolute.array.int32.1 %items.value2, 2
  %_count.address6 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value7 = load i32, ptr %_count.address6, align 4
  %sub = sub i32 %_count.value7, 1
  %array.index.wide = sext i32 %sub to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension5
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.pointee = call ptr @absolute_managed_require(i64 %managed.handle)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.pointee, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.pointee, ptr @string.literal.18)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %if.body
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i32 0

error.continue:                                   ; preds = %if.body
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %0 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %0, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret i32 0

array.bounds.success:                             ; preds = %if.end
  %array.element.address = getelementptr inbounds i32, ptr %array.data3, i32 %sub
  %array.element = load i32, ptr %array.element.address, align 4
  ret i32 %array.element

array.bounds.failure:                             ; preds = %if.end
  %1 = call i32 @puts(ptr @array.bounds.message.19)
  call void @exit(i32 1)
  unreachable
}

define i32 @"std.collections.Vector<int32>.__absolute_property_get_capacity"(ptr %this) {
entry:
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 3
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  ret i32 %_capacity.value
}

define i1 @"std.collections.Vector<int32>.__absolute_property_get_isEmpty"(ptr %this) {
entry:
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %equal = icmp eq i32 %_count.value, 0
  ret i1 %equal
}

define %absolute.array.int32.1 @"std.collections.Vector<int32>.unsafeCopyRaw"(ptr %this) {
entry:
  %toArray.result = call %absolute.array.int32.1 @"std.collections.Vector<int32>.toArray"(ptr %this)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  ret %absolute.array.int32.1 zeroinitializer

error.continue:                                   ; preds = %entry
  %array.data = extractvalue %absolute.array.int32.1 %toArray.result, 0
  %array.owner = extractvalue %absolute.array.int32.1 %toArray.result, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %toArray.result, 2
  ret %absolute.array.int32.1 %toArray.result
}

define i32 @"std.collections.Vector<int32>.pop"(ptr %this) {
entry:
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %equal = icmp eq i32 %_count.value, 0
  br i1 %equal, label %if.body, label %if.end

if.end:                                           ; preds = %entry
  %_count.address1 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.address2 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value3 = load i32, ptr %_count.address2, align 4
  %sub = sub i32 %_count.value3, 1
  store i32 %sub, ptr %_count.address1, align 4
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %items.value, 2
  %items.address4 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value5 = load %absolute.array.int32.1, ptr %items.address4, align 8
  %array.data6 = extractvalue %absolute.array.int32.1 %items.value5, 0
  %array.owner7 = extractvalue %absolute.array.int32.1 %items.value5, 1
  %array.dimension8 = extractvalue %absolute.array.int32.1 %items.value5, 2
  %_count.address9 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value10 = load i32, ptr %_count.address9, align 4
  %array.index.wide = sext i32 %_count.value10 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension8
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.pointee = call ptr @absolute_managed_require(i64 %managed.handle)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.pointee, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.pointee, ptr @string.literal.20)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %if.body
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i32 0

error.continue:                                   ; preds = %if.body
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %0 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %0, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret i32 0

array.bounds.success:                             ; preds = %if.end
  %array.element.address = getelementptr inbounds i32, ptr %array.data6, i32 %_count.value10
  %array.element = load i32, ptr %array.element.address, align 4
  ret i32 %array.element

array.bounds.failure:                             ; preds = %if.end
  %1 = call i32 @puts(ptr @array.bounds.message.21)
  call void @exit(i32 1)
  unreachable
}

define i32 @"std.collections.Vector<int32>.__absolute_property_get_first"(ptr %this) {
entry:
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %equal = icmp eq i32 %_count.value, 0
  br i1 %equal, label %if.body, label %if.end

if.end:                                           ; preds = %entry
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %items.value, 2
  %items.address1 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value2 = load %absolute.array.int32.1, ptr %items.address1, align 8
  %array.data3 = extractvalue %absolute.array.int32.1 %items.value2, 0
  %array.owner4 = extractvalue %absolute.array.int32.1 %items.value2, 1
  %array.dimension5 = extractvalue %absolute.array.int32.1 %items.value2, 2
  %array.index.valid = icmp ult i64 0, %array.dimension5
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.pointee = call ptr @absolute_managed_require(i64 %managed.handle)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.pointee, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.pointee, ptr @string.literal.22)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %if.body
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i32 0

error.continue:                                   ; preds = %if.body
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %0 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %0, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret i32 0

array.bounds.success:                             ; preds = %if.end
  %array.element.address = getelementptr inbounds i32, ptr %array.data3, i32 0
  %array.element = load i32, ptr %array.element.address, align 4
  ret i32 %array.element

array.bounds.failure:                             ; preds = %if.end
  %1 = call i32 @puts(ptr @array.bounds.message.23)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.Vector<int32>.push$int32"(ptr %this, i32 %element) {
entry:
  %i = alloca i32, align 4
  %newItems.array.owner = alloca ptr, align 8
  %newCap = alloca i32, align 4
  %element1 = alloca i32, align 4
  store i32 %element, ptr %element1, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 3
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %equal = icmp eq i32 %_count.value, %_capacity.value
  br i1 %equal, label %if.body, label %if.end

if.end:                                           ; preds = %while.end, %entry
  %items.address37 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value38 = load %absolute.array.int32.1, ptr %items.address37, align 8
  %array.data39 = extractvalue %absolute.array.int32.1 %items.value38, 0
  %array.owner40 = extractvalue %absolute.array.int32.1 %items.value38, 1
  %array.dimension41 = extractvalue %absolute.array.int32.1 %items.value38, 2
  %items.address42 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value43 = load %absolute.array.int32.1, ptr %items.address42, align 8
  %array.data44 = extractvalue %absolute.array.int32.1 %items.value43, 0
  %array.owner45 = extractvalue %absolute.array.int32.1 %items.value43, 1
  %array.dimension46 = extractvalue %absolute.array.int32.1 %items.value43, 2
  %_count.address47 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value48 = load i32, ptr %_count.address47, align 4
  %array.index.wide49 = sext i32 %_count.value48 to i64
  %array.index.valid50 = icmp ult i64 %array.index.wide49, %array.dimension46
  br i1 %array.index.valid50, label %array.bounds.success51, label %array.bounds.failure52, !prof !0

if.body:                                          ; preds = %entry
  %_capacity.address2 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 3
  %_capacity.value3 = load i32, ptr %_capacity.address2, align 4
  %mul = mul i32 %_capacity.value3, 2
  store i32 %mul, ptr %newCap, align 4
  %newCap.value = load i32, ptr %newCap, align 4
  %int.cast = sext i32 %newCap.value to i64
  %array.alloc.bytes = mul i64 %int.cast, 4
  %array.data.alloc = call ptr @malloc(i64 %array.alloc.bytes)
  %array.data = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc, 0
  %array.owner = insertvalue %absolute.array.int32.1 %array.data, ptr %array.data.alloc, 1
  %array.dimension = insertvalue %absolute.array.int32.1 %array.owner, i64 %int.cast, 2
  %array.data4 = extractvalue %absolute.array.int32.1 %array.dimension, 0
  %array.owner5 = extractvalue %absolute.array.int32.1 %array.dimension, 1
  %array.dimension6 = extractvalue %absolute.array.int32.1 %array.dimension, 2
  store ptr %array.owner5, ptr %newItems.array.owner, align 8
  %array.data7 = insertvalue %absolute.array.int32.1 undef, ptr %array.data4, 0
  %array.owner8 = insertvalue %absolute.array.int32.1 %array.data7, ptr %array.owner5, 1
  %array.dimension9 = insertvalue %absolute.array.int32.1 %array.owner8, i64 %array.dimension6, 2
  store i32 0, ptr %i, align 4
  br label %while.condition

while.condition:                                  ; preds = %array.bounds.success26, %if.body
  %i.value = load i32, ptr %i, align 4
  %_count.address10 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value11 = load i32, ptr %_count.address10, align 4
  %less = icmp slt i32 %i.value, %_count.value11
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %newItems.array.owner12 = load ptr, ptr %newItems.array.owner, align 8
  %newItems.array.owner13 = load ptr, ptr %newItems.array.owner, align 8
  %i.value14 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value14 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension6
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %items.address30 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %newItems.array.owner31 = load ptr, ptr %newItems.array.owner, align 8
  %array.data32 = insertvalue %absolute.array.int32.1 undef, ptr %array.data4, 0
  %array.owner33 = insertvalue %absolute.array.int32.1 %array.data32, ptr %newItems.array.owner31, 1
  %array.dimension34 = insertvalue %absolute.array.int32.1 %array.owner33, i64 %array.dimension6, 2
  store ptr null, ptr %newItems.array.owner, align 8
  %field.cleanup.array = load %absolute.array.int32.1, ptr %items.address30, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %items.address30, align 8
  store %absolute.array.int32.1 %array.dimension34, ptr %items.address30, align 8
  %_capacity.address35 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 3
  %newCap.value36 = load i32, ptr %newCap, align 4
  store i32 %newCap.value36, ptr %_capacity.address35, align 4
  br label %if.end

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data4, i32 %i.value14
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data15 = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner16 = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension17 = extractvalue %absolute.array.int32.1 %items.value, 2
  %items.address18 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value19 = load %absolute.array.int32.1, ptr %items.address18, align 8
  %array.data20 = extractvalue %absolute.array.int32.1 %items.value19, 0
  %array.owner21 = extractvalue %absolute.array.int32.1 %items.value19, 1
  %array.dimension22 = extractvalue %absolute.array.int32.1 %items.value19, 2
  %i.value23 = load i32, ptr %i, align 4
  %array.index.wide24 = sext i32 %i.value23 to i64
  %array.index.valid25 = icmp ult i64 %array.index.wide24, %array.dimension22
  br i1 %array.index.valid25, label %array.bounds.success26, label %array.bounds.failure27, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.24)
  call void @exit(i32 1)
  unreachable

array.bounds.success26:                           ; preds = %array.bounds.success
  %array.element.address28 = getelementptr inbounds i32, ptr %array.data20, i32 %i.value23
  %array.element = load i32, ptr %array.element.address28, align 4
  store i32 %array.element, ptr %array.element.address, align 4
  %i.value29 = load i32, ptr %i, align 4
  %add = add i32 %i.value29, 1
  store i32 %add, ptr %i, align 4
  br label %while.condition

array.bounds.failure27:                           ; preds = %array.bounds.success
  %1 = call i32 @puts(ptr @array.bounds.message.25)
  call void @exit(i32 1)
  unreachable

array.bounds.success51:                           ; preds = %if.end
  %array.element.address53 = getelementptr inbounds i32, ptr %array.data44, i32 %_count.value48
  %move.value = load i32, ptr %element1, align 4
  call void @llvm.memset.p0.i64(ptr align 8 %element1, i8 0, i64 4, i1 false)
  store i32 %move.value, ptr %array.element.address53, align 4
  %_count.address54 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.address55 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value56 = load i32, ptr %_count.address55, align 4
  %add57 = add i32 %_count.value56, 1
  store i32 %add57, ptr %_count.address54, align 4
  ret void

array.bounds.failure52:                           ; preds = %if.end
  %2 = call i32 @puts(ptr @array.bounds.message.26)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.Vector<int32>.reserve$int32"(ptr %this, i32 %requestedCapacity) {
entry:
  %i = alloca i32, align 4
  %newItems.array.owner = alloca ptr, align 8
  %requestedCapacity1 = alloca i32, align 4
  store i32 %requestedCapacity, ptr %requestedCapacity1, align 4
  %requestedCapacity.value = load i32, ptr %requestedCapacity1, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 3
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %less.equal = icmp sle i32 %requestedCapacity.value, %_capacity.value
  br i1 %less.equal, label %if.body, label %if.end

if.end:                                           ; preds = %entry
  %requestedCapacity.value2 = load i32, ptr %requestedCapacity1, align 4
  %int.cast = sext i32 %requestedCapacity.value2 to i64
  %array.alloc.bytes = mul i64 %int.cast, 4
  %array.data.alloc = call ptr @malloc(i64 %array.alloc.bytes)
  %array.data = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc, 0
  %array.owner = insertvalue %absolute.array.int32.1 %array.data, ptr %array.data.alloc, 1
  %array.dimension = insertvalue %absolute.array.int32.1 %array.owner, i64 %int.cast, 2
  %array.data3 = extractvalue %absolute.array.int32.1 %array.dimension, 0
  %array.owner4 = extractvalue %absolute.array.int32.1 %array.dimension, 1
  %array.dimension5 = extractvalue %absolute.array.int32.1 %array.dimension, 2
  store ptr %array.owner4, ptr %newItems.array.owner, align 8
  %array.data6 = insertvalue %absolute.array.int32.1 undef, ptr %array.data3, 0
  %array.owner7 = insertvalue %absolute.array.int32.1 %array.data6, ptr %array.owner4, 1
  %array.dimension8 = insertvalue %absolute.array.int32.1 %array.owner7, i64 %array.dimension5, 2
  store i32 0, ptr %i, align 4
  br label %while.condition

if.body:                                          ; preds = %entry
  ret void

while.condition:                                  ; preds = %array.bounds.success23, %if.end
  %i.value = load i32, ptr %i, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %less = icmp slt i32 %i.value, %_count.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %newItems.array.owner9 = load ptr, ptr %newItems.array.owner, align 8
  %newItems.array.owner10 = load ptr, ptr %newItems.array.owner, align 8
  %i.value11 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value11 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension5
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %items.address27 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %newItems.array.owner28 = load ptr, ptr %newItems.array.owner, align 8
  %array.data29 = insertvalue %absolute.array.int32.1 undef, ptr %array.data3, 0
  %array.owner30 = insertvalue %absolute.array.int32.1 %array.data29, ptr %newItems.array.owner28, 1
  %array.dimension31 = insertvalue %absolute.array.int32.1 %array.owner30, i64 %array.dimension5, 2
  store ptr null, ptr %newItems.array.owner, align 8
  %field.cleanup.array = load %absolute.array.int32.1, ptr %items.address27, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %items.address27, align 8
  store %absolute.array.int32.1 %array.dimension31, ptr %items.address27, align 8
  %_capacity.address32 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 3
  %requestedCapacity.value33 = load i32, ptr %requestedCapacity1, align 4
  store i32 %requestedCapacity.value33, ptr %_capacity.address32, align 4
  ret void

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data3, i32 %i.value11
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data12 = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner13 = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension14 = extractvalue %absolute.array.int32.1 %items.value, 2
  %items.address15 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value16 = load %absolute.array.int32.1, ptr %items.address15, align 8
  %array.data17 = extractvalue %absolute.array.int32.1 %items.value16, 0
  %array.owner18 = extractvalue %absolute.array.int32.1 %items.value16, 1
  %array.dimension19 = extractvalue %absolute.array.int32.1 %items.value16, 2
  %i.value20 = load i32, ptr %i, align 4
  %array.index.wide21 = sext i32 %i.value20 to i64
  %array.index.valid22 = icmp ult i64 %array.index.wide21, %array.dimension19
  br i1 %array.index.valid22, label %array.bounds.success23, label %array.bounds.failure24, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.27)
  call void @exit(i32 1)
  unreachable

array.bounds.success23:                           ; preds = %array.bounds.success
  %array.element.address25 = getelementptr inbounds i32, ptr %array.data17, i32 %i.value20
  %array.element = load i32, ptr %array.element.address25, align 4
  store i32 %array.element, ptr %array.element.address, align 4
  %i.value26 = load i32, ptr %i, align 4
  %add = add i32 %i.value26, 1
  store i32 %add, ptr %i, align 4
  br label %while.condition

array.bounds.failure24:                           ; preds = %array.bounds.success
  %1 = call i32 @puts(ptr @array.bounds.message.28)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.Vector<int32>.clear"(ptr %this) {
entry:
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  store i32 0, ptr %_count.address, align 4
  ret void
}

define i64 @"std.collections.Vector<int32>.builder"(ptr %this) {
entry:
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.VectorBuilder<int32>", ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 620979060937029473)
  %managed.pointee = call ptr @absolute_managed_require(i64 %managed.handle)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee, i8 0, i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.VectorBuilder<int32>", ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %managed.pointee, i32 0, i32 0
  store ptr @"std.collections.VectorBuilder<int32>.__vtable", ptr %vtable.address, align 8
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  call void @"std.collections.VectorBuilder<int32>.__ctor$int32_5B_5D$int32"(ptr %managed.pointee, %absolute.array.int32.1 %items.value, i1 false, i32 %_count.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i64 0

error.continue:                                   ; preds = %entry
  ret i64 %managed.handle
}

define void @"std.collections.Vector<int32>.removeAt$int32"(ptr %this, i32 %index) {
entry:
  %i = alloca i32, align 4
  %index1 = alloca i32, align 4
  store i32 %index, ptr %index1, align 4
  %index.value = load i32, ptr %index1, align 4
  %less = icmp slt i32 %index.value, 0
  %index.value2 = load i32, ptr %index1, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %greater.equal = icmp sge i32 %index.value2, %_count.value
  %logical.or = or i1 %less, %greater.equal
  br i1 %logical.or, label %if.body, label %if.end

if.end:                                           ; preds = %entry
  %index.value3 = load i32, ptr %index1, align 4
  store i32 %index.value3, ptr %i, align 4
  br label %while.condition

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.pointee = call ptr @absolute_managed_require(i64 %managed.handle)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.pointee, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.pointee, ptr @string.literal.29)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %if.body
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret void

error.continue:                                   ; preds = %if.body
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %0 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %0, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret void

while.condition:                                  ; preds = %array.bounds.success26, %if.end
  %i.value = load i32, ptr %i, align 4
  %_count.address4 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value5 = load i32, ptr %_count.address4, align 4
  %sub = sub i32 %_count.value5, 1
  %less6 = icmp slt i32 %i.value, %sub
  br i1 %less6, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %items.value, 2
  %items.address7 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value8 = load %absolute.array.int32.1, ptr %items.address7, align 8
  %array.data9 = extractvalue %absolute.array.int32.1 %items.value8, 0
  %array.owner10 = extractvalue %absolute.array.int32.1 %items.value8, 1
  %array.dimension11 = extractvalue %absolute.array.int32.1 %items.value8, 2
  %i.value12 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value12 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension11
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %_count.address31 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.address32 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value33 = load i32, ptr %_count.address32, align 4
  %sub34 = sub i32 %_count.value33, 1
  store i32 %sub34, ptr %_count.address31, align 4
  ret void

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data9, i32 %i.value12
  %items.address13 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value14 = load %absolute.array.int32.1, ptr %items.address13, align 8
  %array.data15 = extractvalue %absolute.array.int32.1 %items.value14, 0
  %array.owner16 = extractvalue %absolute.array.int32.1 %items.value14, 1
  %array.dimension17 = extractvalue %absolute.array.int32.1 %items.value14, 2
  %items.address18 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value19 = load %absolute.array.int32.1, ptr %items.address18, align 8
  %array.data20 = extractvalue %absolute.array.int32.1 %items.value19, 0
  %array.owner21 = extractvalue %absolute.array.int32.1 %items.value19, 1
  %array.dimension22 = extractvalue %absolute.array.int32.1 %items.value19, 2
  %i.value23 = load i32, ptr %i, align 4
  %add = add i32 %i.value23, 1
  %array.index.wide24 = sext i32 %add to i64
  %array.index.valid25 = icmp ult i64 %array.index.wide24, %array.dimension22
  br i1 %array.index.valid25, label %array.bounds.success26, label %array.bounds.failure27, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %1 = call i32 @puts(ptr @array.bounds.message.30)
  call void @exit(i32 1)
  unreachable

array.bounds.success26:                           ; preds = %array.bounds.success
  %array.element.address28 = getelementptr inbounds i32, ptr %array.data20, i32 %add
  %array.element = load i32, ptr %array.element.address28, align 4
  store i32 %array.element, ptr %array.element.address, align 4
  %i.value29 = load i32, ptr %i, align 4
  %add30 = add i32 %i.value29, 1
  store i32 %add30, ptr %i, align 4
  br label %while.condition

array.bounds.failure27:                           ; preds = %array.bounds.success
  %2 = call i32 @puts(ptr @array.bounds.message.31)
  call void @exit(i32 1)
  unreachable
}

define i32 @"std.collections.Vector<int32>.__absolute_indexer_get$int32"(ptr %this, i32 %index) {
entry:
  %index1 = alloca i32, align 4
  store i32 %index, ptr %index1, align 4
  %index.value = load i32, ptr %index1, align 4
  %greater.equal = icmp sge i32 %index.value, 0
  %index.value2 = load i32, ptr %index1, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %less = icmp slt i32 %index.value2, %_count.value
  %logical.and = and i1 %greater.equal, %less
  br i1 %logical.and, label %assert.success, label %assert.failure

assert.success:                                   ; preds = %entry
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %items.value, 2
  %index.value3 = load i32, ptr %index1, align 4
  %unsafe.array.index = sext i32 %index.value3 to i64
  %unsafe.array.element.address = getelementptr inbounds i32, ptr %array.data, i64 %unsafe.array.index
  %unsafe.array.element = load i32, ptr %unsafe.array.element.address, align 4
  ret i32 %unsafe.array.element

assert.failure:                                   ; preds = %entry
  %print.result = call i32 (ptr, ...) @printf(ptr @print.format.33, ptr @string.literal.32)
  call void @abort()
  unreachable
}

define void @"std.collections.Vector<int32>.__absolute_indexer_set$int32$int32"(ptr %this, i32 %index, i32 %value) {
entry:
  %value2 = alloca i32, align 4
  %index1 = alloca i32, align 4
  store i32 %index, ptr %index1, align 4
  store i32 %value, ptr %value2, align 4
  %index.value = load i32, ptr %index1, align 4
  %greater.equal = icmp sge i32 %index.value, 0
  %index.value3 = load i32, ptr %index1, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %less = icmp slt i32 %index.value3, %_count.value
  %logical.and = and i1 %greater.equal, %less
  br i1 %logical.and, label %assert.success, label %assert.failure

assert.success:                                   ; preds = %entry
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %items.value, 2
  %index.value4 = load i32, ptr %index1, align 4
  %unsafe.array.index = sext i32 %index.value4 to i64
  %unsafe.array.element.address = getelementptr inbounds i32, ptr %array.data, i64 %unsafe.array.index
  %move.value = load i32, ptr %value2, align 4
  call void @llvm.memset.p0.i64(ptr align 8 %value2, i8 0, i64 4, i1 false)
  store i32 %move.value, ptr %unsafe.array.element.address, align 4
  ret void

assert.failure:                                   ; preds = %entry
  %print.result = call i32 (ptr, ...) @printf(ptr @print.format.36, ptr @string.literal.34)
  call void @abort()
  unreachable
}

define ptr @"std.collections.Vector<int32>.unsafeData"(ptr %this) {
entry:
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %items.value, 2
  ret ptr %array.data
}

define i64 @"std.collections.Vector<int32>.iterate"(ptr %this) {
entry:
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.VectorIterator<int32>", ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 -1630790955709830350)
  %managed.pointee = call ptr @absolute_managed_require(i64 %managed.handle)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee, i8 0, i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.VectorIterator<int32>", ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %managed.pointee, i32 0, i32 0
  store ptr @"std.collections.VectorIterator<int32>.__vtable", ptr %vtable.address, align 8
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  call void @"std.collections.VectorIterator<int32>.__ctor$int32_5B_5D$int32"(ptr %managed.pointee, %absolute.array.int32.1 %items.value, i1 false, i32 %_count.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i64 0

error.continue:                                   ; preds = %entry
  ret i64 %managed.handle
}

define %absolute.array.int32.1 @"std.collections.Vector<int32>.toArray"(ptr %this) {
entry:
  %i = alloca i32, align 4
  %res.array.owner = alloca ptr, align 8
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %int.cast = sext i32 %_count.value to i64
  %array.alloc.bytes = mul i64 %int.cast, 4
  %array.data.alloc = call ptr @malloc(i64 %array.alloc.bytes)
  %array.data = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc, 0
  %array.owner = insertvalue %absolute.array.int32.1 %array.data, ptr %array.data.alloc, 1
  %array.dimension = insertvalue %absolute.array.int32.1 %array.owner, i64 %int.cast, 2
  %array.data1 = extractvalue %absolute.array.int32.1 %array.dimension, 0
  %array.owner2 = extractvalue %absolute.array.int32.1 %array.dimension, 1
  %array.dimension3 = extractvalue %absolute.array.int32.1 %array.dimension, 2
  store ptr %array.owner2, ptr %res.array.owner, align 8
  %array.data4 = insertvalue %absolute.array.int32.1 undef, ptr %array.data1, 0
  %array.owner5 = insertvalue %absolute.array.int32.1 %array.data4, ptr %array.owner2, 1
  %array.dimension6 = insertvalue %absolute.array.int32.1 %array.owner5, i64 %array.dimension3, 2
  store i32 0, ptr %i, align 4
  br label %while.condition

while.condition:                                  ; preds = %array.bounds.success23, %entry
  %i.value = load i32, ptr %i, align 4
  %_count.address7 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value8 = load i32, ptr %_count.address7, align 4
  %less = icmp slt i32 %i.value, %_count.value8
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %res.array.owner9 = load ptr, ptr %res.array.owner, align 8
  %res.array.owner10 = load ptr, ptr %res.array.owner, align 8
  %i.value11 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value11 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension3
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %res.array.owner27 = load ptr, ptr %res.array.owner, align 8
  %array.data28 = insertvalue %absolute.array.int32.1 undef, ptr %array.data1, 0
  %array.owner29 = insertvalue %absolute.array.int32.1 %array.data28, ptr %res.array.owner27, 1
  %array.dimension30 = insertvalue %absolute.array.int32.1 %array.owner29, i64 %array.dimension3, 2
  ret %absolute.array.int32.1 %array.dimension30

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data1, i32 %i.value11
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data12 = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner13 = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension14 = extractvalue %absolute.array.int32.1 %items.value, 2
  %items.address15 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value16 = load %absolute.array.int32.1, ptr %items.address15, align 8
  %array.data17 = extractvalue %absolute.array.int32.1 %items.value16, 0
  %array.owner18 = extractvalue %absolute.array.int32.1 %items.value16, 1
  %array.dimension19 = extractvalue %absolute.array.int32.1 %items.value16, 2
  %i.value20 = load i32, ptr %i, align 4
  %array.index.wide21 = sext i32 %i.value20 to i64
  %array.index.valid22 = icmp ult i64 %array.index.wide21, %array.dimension19
  br i1 %array.index.valid22, label %array.bounds.success23, label %array.bounds.failure24, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.37)
  call void @exit(i32 1)
  unreachable

array.bounds.success23:                           ; preds = %array.bounds.success
  %array.element.address25 = getelementptr inbounds i32, ptr %array.data17, i32 %i.value20
  %array.element = load i32, ptr %array.element.address25, align 4
  store i32 %array.element, ptr %array.element.address, align 4
  %i.value26 = load i32, ptr %i, align 4
  %add = add i32 %i.value26, 1
  store i32 %add, ptr %i, align 4
  br label %while.condition

array.bounds.failure24:                           ; preds = %array.bounds.success
  %1 = call i32 @puts(ptr @array.bounds.message.38)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.Vector<int32>.__ctor"(ptr %this) {
entry:
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  store i32 0, ptr %_count.address, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 3
  store i32 4, ptr %_capacity.address, align 4
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %array.data.alloc = call ptr @malloc(i64 16)
  %array.data = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc, 0
  %array.owner = insertvalue %absolute.array.int32.1 %array.data, ptr %array.data.alloc, 1
  %array.dimension = insertvalue %absolute.array.int32.1 %array.owner, i64 4, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %items.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %items.address, align 8
  store %absolute.array.int32.1 %array.dimension, ptr %items.address, align 8
  ret void
}

define void @"std.collections.Vector<int32>.__ctor$int32"(ptr %this, i32 %initialCapacity) {
entry:
  %initialCapacity1 = alloca i32, align 4
  store i32 %initialCapacity, ptr %initialCapacity1, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  store i32 0, ptr %_count.address, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 3
  %initialCapacity.value = load i32, ptr %initialCapacity1, align 4
  %greater = icmp sgt i32 %initialCapacity.value, 0
  br i1 %greater, label %ternary.true, label %ternary.false

ternary.true:                                     ; preds = %entry
  %initialCapacity.value2 = load i32, ptr %initialCapacity1, align 4
  br label %ternary.end

ternary.false:                                    ; preds = %entry
  br label %ternary.end

ternary.end:                                      ; preds = %ternary.false, %ternary.true
  %ternary.result = phi i32 [ %initialCapacity.value2, %ternary.true ], [ 4, %ternary.false ]
  store i32 %ternary.result, ptr %_capacity.address, align 4
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %_capacity.address3 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 3
  %_capacity.value = load i32, ptr %_capacity.address3, align 4
  %int.cast = sext i32 %_capacity.value to i64
  %array.alloc.bytes = mul i64 %int.cast, 4
  %array.data.alloc = call ptr @malloc(i64 %array.alloc.bytes)
  %array.data = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc, 0
  %array.owner = insertvalue %absolute.array.int32.1 %array.data, ptr %array.data.alloc, 1
  %array.dimension = insertvalue %absolute.array.int32.1 %array.owner, i64 %int.cast, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %items.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %items.address, align 8
  store %absolute.array.int32.1 %array.dimension, ptr %items.address, align 8
  ret void
}

define internal void @"std.collections.Vector<int32>.__destroy"(ptr %this) {
entry:
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %field.cleanup.array = load %absolute.array.int32.1, ptr %items.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %items.address, align 8
  ret void
}

define i32 @main() {
entry:
  %last = alloca i32, align 4
  %first = alloca i32, align 4
  %checksum = alloca i64, align 8
  %cur = alloca i32, align 4
  %j = alloca i32, align 4
  %key = alloca i32, align 4
  %outer = alloca i32, align 4
  %data = alloca ptr, align 8
  %i = alloca i32, align 4
  %state = alloca i32, align 4
  %values.cached.pointee = alloca ptr, align 8
  %values = alloca i64, align 8
  %SIZE = alloca i32, align 4
  store i32 100000, ptr %SIZE, align 4
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.Vector<int32>", ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 -6152174195361087120)
  %managed.pointee = call ptr @absolute_managed_require(i64 %managed.handle)
  call void @llvm.memset.p0.i64(ptr align 8 %managed.pointee, i8 0, i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.Vector<int32>", ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %managed.pointee, i32 0, i32 0
  store ptr @"std.collections.Vector<int32>.__vtable", ptr %vtable.address, align 8
  %SIZE.value = load i32, ptr %SIZE, align 4
  call void @"std.collections.Vector<int32>.__ctor$int32"(ptr %managed.pointee, i32 %SIZE.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  call void @absolute_managed_destroy(i64 %managed.handle)
  %cleanup.handle = load i64, ptr %values, align 4
  %managed.pointee1 = call ptr @absolute_managed_get(i64 %cleanup.handle)
  %aggregate.present = icmp ne ptr %managed.pointee1, null
  br i1 %aggregate.present, label %aggregate.cleanup, label %aggregate.cleanup.end

error.continue:                                   ; preds = %entry
  store i64 %managed.handle, ptr %values, align 4
  store ptr %managed.pointee, ptr %values.cached.pointee, align 8
  store i32 123456789, ptr %state, align 4
  store i32 0, ptr %i, align 4
  br label %while.condition

aggregate.cleanup:                                ; preds = %error.propagate
  %aggregate.vtable = load ptr, ptr %managed.pointee1, align 8
  %aggregate.destroy.slot = getelementptr ptr, ptr %aggregate.vtable, i64 0
  %aggregate.destroy = load ptr, ptr %aggregate.destroy.slot, align 8
  call void %aggregate.destroy(ptr %managed.pointee1)
  br label %aggregate.cleanup.end

aggregate.cleanup.end:                            ; preds = %aggregate.cleanup, %error.propagate
  call void @absolute_managed_destroy(i64 %cleanup.handle)
  store i64 0, ptr %values, align 4
  call void @absolute_error_report()
  ret i32 1

while.condition:                                  ; preds = %while.body, %error.continue
  %i.value = load i32, ptr %i, align 4
  %SIZE.value2 = load i32, ptr %SIZE, align 4
  %less = icmp slt i32 %i.value, %SIZE.value2
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %state.value = load i32, ptr %state, align 4
  %mul = mul i32 %state.value, 1664525
  %add = add i32 %mul, 1013904223
  store i32 %add, ptr %state, align 4
  %values.value = load i64, ptr %values, align 4
  %values.cached.pointee3 = load ptr, ptr %values.cached.pointee, align 8
  %state.value4 = load i32, ptr %state, align 4
  call void @"std.collections.Vector<int32>.push$int32"(ptr %values.cached.pointee3, i32 %state.value4)
  %assignment.current = load i32, ptr %i, align 4
  %add5 = add i32 %assignment.current, 1
  store i32 %add5, ptr %i, align 4
  br label %while.condition

while.end:                                        ; preds = %while.condition
  %values.value6 = load i64, ptr %values, align 4
  %values.cached.pointee7 = load ptr, ptr %values.cached.pointee, align 8
  %unsafeData.result = call ptr @"std.collections.Vector<int32>.unsafeData"(ptr %values.cached.pointee7)
  store ptr %unsafeData.result, ptr %data, align 8
  store i32 1, ptr %outer, align 4
  br label %while.condition8

while.condition8:                                 ; preds = %while.end17, %while.end
  %outer.value = load i32, ptr %outer, align 4
  %SIZE.value11 = load i32, ptr %SIZE, align 4
  %less12 = icmp slt i32 %outer.value, %SIZE.value11
  br i1 %less12, label %while.body9, label %while.end10

while.body9:                                      ; preds = %while.condition8
  %data.value = load ptr, ptr %data, align 8
  %outer.value13 = load i32, ptr %outer, align 4
  %int.cast = sext i32 %outer.value13 to i64
  %pointer.offset = getelementptr i32, ptr %data.value, i64 %int.cast
  %pointer.value = load i32, ptr %pointer.offset, align 4
  store i32 %pointer.value, ptr %key, align 4
  %outer.value14 = load i32, ptr %outer, align 4
  %sub = sub i32 %outer.value14, 1
  store i32 %sub, ptr %j, align 4
  br label %while.condition15

while.end10:                                      ; preds = %while.condition8
  store i64 0, ptr %checksum, align 4
  store i32 0, ptr %i, align 4
  br label %while.condition39

while.condition15:                                ; preds = %if.end, %while.body9
  %j.value = load i32, ptr %j, align 4
  %greater.equal = icmp sge i32 %j.value, 0
  br i1 %greater.equal, label %while.body16, label %while.end17

while.body16:                                     ; preds = %while.condition15
  %data.value18 = load ptr, ptr %data, align 8
  %j.value19 = load i32, ptr %j, align 4
  %int.cast20 = sext i32 %j.value19 to i64
  %pointer.offset21 = getelementptr i32, ptr %data.value18, i64 %int.cast20
  %pointer.value22 = load i32, ptr %pointer.offset21, align 4
  store i32 %pointer.value22, ptr %cur, align 4
  %cur.value = load i32, ptr %cur, align 4
  %key.value = load i32, ptr %key, align 4
  %less.equal = icmp sle i32 %cur.value, %key.value
  br i1 %less.equal, label %if.body, label %if.end

while.end17:                                      ; preds = %if.body, %while.condition15
  %data.value31 = load ptr, ptr %data, align 8
  %j.value32 = load i32, ptr %j, align 4
  %add33 = add i32 %j.value32, 1
  %int.cast34 = sext i32 %add33 to i64
  %pointer.offset35 = getelementptr i32, ptr %data.value31, i64 %int.cast34
  %key.value36 = load i32, ptr %key, align 4
  store i32 %key.value36, ptr %pointer.offset35, align 4
  %assignment.current37 = load i32, ptr %outer, align 4
  %add38 = add i32 %assignment.current37, 1
  store i32 %add38, ptr %outer, align 4
  br label %while.condition8, !llvm.loop !1

if.end:                                           ; preds = %while.body16
  %data.value23 = load ptr, ptr %data, align 8
  %j.value24 = load i32, ptr %j, align 4
  %add25 = add i32 %j.value24, 1
  %int.cast26 = sext i32 %add25 to i64
  %pointer.offset27 = getelementptr i32, ptr %data.value23, i64 %int.cast26
  %cur.value28 = load i32, ptr %cur, align 4
  store i32 %cur.value28, ptr %pointer.offset27, align 4
  %assignment.current29 = load i32, ptr %j, align 4
  %sub30 = sub i32 %assignment.current29, 1
  store i32 %sub30, ptr %j, align 4
  br label %while.condition15

if.body:                                          ; preds = %while.body16
  br label %while.end17

while.condition39:                                ; preds = %while.body40, %while.end10
  %i.value42 = load i32, ptr %i, align 4
  %SIZE.value43 = load i32, ptr %SIZE, align 4
  %less44 = icmp slt i32 %i.value42, %SIZE.value43
  br i1 %less44, label %while.body40, label %while.end41

while.body40:                                     ; preds = %while.condition39
  %data.value45 = load ptr, ptr %data, align 8
  %i.value46 = load i32, ptr %i, align 4
  %int.cast47 = sext i32 %i.value46 to i64
  %pointer.offset48 = getelementptr i32, ptr %data.value45, i64 %int.cast47
  %pointer.value49 = load i32, ptr %pointer.offset48, align 4
  %assignment.current50 = load i64, ptr %checksum, align 4
  %int.cast51 = sext i32 %pointer.value49 to i64
  %add52 = add i64 %assignment.current50, %int.cast51
  store i64 %add52, ptr %checksum, align 4
  %assignment.current53 = load i32, ptr %i, align 4
  %add54 = add i32 %assignment.current53, 1
  store i32 %add54, ptr %i, align 4
  br label %while.condition39

while.end41:                                      ; preds = %while.condition39
  %data.value55 = load ptr, ptr %data, align 8
  %pointer.value56 = load i32, ptr %data.value55, align 4
  store i32 %pointer.value56, ptr %first, align 4
  %data.value57 = load ptr, ptr %data, align 8
  %SIZE.value58 = load i32, ptr %SIZE, align 4
  %sub59 = sub i32 %SIZE.value58, 1
  %int.cast60 = sext i32 %sub59 to i64
  %pointer.offset61 = getelementptr i32, ptr %data.value57, i64 %int.cast60
  %pointer.value62 = load i32, ptr %pointer.offset61, align 4
  store i32 %pointer.value62, ptr %last, align 4
  %first.value = load i32, ptr %first, align 4
  %int.cast63 = sext i32 %first.value to i64
  %mul64 = mul i64 %int.cast63, 31
  %assignment.current65 = load i64, ptr %checksum, align 4
  %add66 = add i64 %assignment.current65, %mul64
  store i64 %add66, ptr %checksum, align 4
  %last.value = load i32, ptr %last, align 4
  %int.cast67 = sext i32 %last.value to i64
  %mul68 = mul i64 %int.cast67, 17
  %assignment.current69 = load i64, ptr %checksum, align 4
  %add70 = add i64 %assignment.current69, %mul68
  store i64 %add70, ptr %checksum, align 4
  %checksum.value = load i64, ptr %checksum, align 4
  %print.result = call i32 (ptr, ...) @printf(ptr @print.format, i64 %checksum.value)
  %cleanup.handle71 = load i64, ptr %values, align 4
  %managed.pointee72 = call ptr @absolute_managed_get(i64 %cleanup.handle71)
  %aggregate.present75 = icmp ne ptr %managed.pointee72, null
  br i1 %aggregate.present75, label %aggregate.cleanup73, label %aggregate.cleanup.end74

aggregate.cleanup73:                              ; preds = %while.end41
  %aggregate.vtable76 = load ptr, ptr %managed.pointee72, align 8
  %aggregate.destroy.slot77 = getelementptr ptr, ptr %aggregate.vtable76, i64 0
  %aggregate.destroy78 = load ptr, ptr %aggregate.destroy.slot77, align 8
  call void %aggregate.destroy78(ptr %managed.pointee72)
  br label %aggregate.cleanup.end74

aggregate.cleanup.end74:                          ; preds = %aggregate.cleanup73, %while.end41
  call void @absolute_managed_destroy(i64 %cleanup.handle71)
  store i64 0, ptr %values, align 4
  store ptr null, ptr %values.cached.pointee, align 8
  ret i32 0
}

declare i64 @absolute_managed_create(i64)

declare void @absolute_managed_set_type(i64, i64)

declare ptr @absolute_managed_require(i64)

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #0

declare i1 @absolute_error_pending()

declare void @absolute_managed_destroy(i64)

declare ptr @absolute_managed_get(i64)

declare void @absolute_error_report()

declare i32 @printf(ptr, ...)

declare i32 @puts(ptr)

; Function Attrs: cold noreturn
declare void @exit(i32) #1

declare ptr @malloc(i64)

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i64(ptr noalias nocapture writeonly, ptr noalias nocapture readonly, i64, i1 immarg) #2

declare void @free(ptr)

declare i64 @absolute_managed_type(i64)

declare void @absolute_error_set(i64, i64)

declare void @abort()

attributes #0 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #1 = { cold noreturn }
attributes #2 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }

!0 = !{!"branch_weights", i32 2000, i32 1}
!1 = distinct !{!1, !2}
!2 = !{!"llvm.loop.unroll.count", i32 8}
