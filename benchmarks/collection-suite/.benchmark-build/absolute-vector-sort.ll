; ModuleID = 'vector-sort.abs'
source_filename = "vector-sort.abs"
target triple = "x86_64-pc-windows-msvc"

%absolute.class.Error = type { ptr, ptr }
%"absolute.class.std.collections.VectorIterator<int32>" = type { ptr, %absolute.array.int32.1, i32, i32 }
%absolute.array.int32.1 = type { ptr, ptr, i64 }
%"absolute.class.std.collections.VectorBuilder<int32>" = type { ptr, %absolute.array.int32.1, i32, i32, i1 }
%absolute.Slot = type { ptr, i32, i32, i64 }
%"absolute.class.std.collections.Vector<int32>" = type { ptr, %absolute.array.int32.1, i32, i32, i1 }

@Error.__vtable = internal constant [1 x ptr] [ptr @Error.__destroy], align 8
@"std.collections.VectorIterator<int32>.__vtable" = internal constant [1 x ptr] [ptr @"std.collections.VectorIterator<int32>.__destroy"], align 8
@"std.collections.VectorBuilder<int32>.__vtable" = internal constant [1 x ptr] [ptr @"std.collections.VectorBuilder<int32>.__destroy"], align 8
@"std.collections.Vector<int32>.__vtable" = internal constant [1 x ptr] [ptr @"std.collections.Vector<int32>.__destroy"], align 8
@absolute_managed_slots_size = external global i32
@absolute_managed_slots_data = external global ptr
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
@array.bounds.message.18 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.19 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.20 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.21 = private unnamed_addr constant [16 x i8] c"Vector is empty\00", align 1
@array.bounds.message.22 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.23 = private unnamed_addr constant [16 x i8] c"Vector is empty\00", align 1
@array.bounds.message.24 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.25 = private unnamed_addr constant [16 x i8] c"Vector is empty\00", align 1
@array.bounds.message.26 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.27 = private unnamed_addr constant [20 x i8] c"Index out of bounds\00", align 1
@array.bounds.message.28 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.29 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.30 = private unnamed_addr constant [20 x i8] c"Index out of bounds\00", align 1
@array.bounds.message.31 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.32 = private unnamed_addr constant [20 x i8] c"Index out of bounds\00", align 1
@array.bounds.message.33 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.34 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.35 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1

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

define void @"std.collections.VectorIterator<int32>.__ctor$int32_5B_5D$int32"(ptr %this, %absolute.array.int32.1 %source, i32 %sourceLength) {
entry:
  %sourceLength1 = alloca i32, align 4
  %array.data = extractvalue %absolute.array.int32.1 %source, 0
  %array.owner = extractvalue %absolute.array.int32.1 %source, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %source, 2
  store i32 %sourceLength, ptr %sourceLength1, align 4
  %snapshot.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 1
  %sourceLength.value = load i32, ptr %sourceLength1, align 4
  %int.cast = sext i32 %sourceLength.value to i64
  %slice.order.valid = icmp sge i64 %int.cast, 0
  %slice.lower.valid = and i1 true, %slice.order.valid
  %slice.end.below.size = icmp sle i64 %int.cast, %array.dimension
  %slice.valid = and i1 %slice.lower.valid, %slice.end.below.size
  br i1 %slice.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

array.bounds.success:                             ; preds = %entry
  %slice.dim.length = sub i64 %int.cast, 0
  %slice.data = getelementptr inbounds i32, ptr %array.data, i64 0
  %array.data2 = insertvalue %absolute.array.int32.1 undef, ptr %slice.data, 0
  %array.owner3 = insertvalue %absolute.array.int32.1 %array.data2, ptr %array.owner, 1
  %array.dimension4 = insertvalue %absolute.array.int32.1 %array.owner3, i64 %slice.dim.length, 2
  %array.data5 = extractvalue %absolute.array.int32.1 %array.dimension4, 0
  %array.owner6 = extractvalue %absolute.array.int32.1 %array.dimension4, 1
  %array.dimension7 = extractvalue %absolute.array.int32.1 %array.dimension4, 2
  %copy.element.count = mul i64 1, %array.dimension7
  %copy.byte.count = mul i64 %copy.element.count, 4
  %copy.data = call ptr @malloc(i64 %copy.byte.count)
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %copy.data, ptr align 1 %array.data5, i64 %copy.byte.count, i1 false)
  %array.data8 = insertvalue %absolute.array.int32.1 undef, ptr %copy.data, 0
  %array.owner9 = insertvalue %absolute.array.int32.1 %array.data8, ptr %copy.data, 1
  %array.dimension10 = insertvalue %absolute.array.int32.1 %array.owner9, i64 %array.dimension7, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %snapshot.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %snapshot.address, align 8
  store %absolute.array.int32.1 %array.dimension10, ptr %snapshot.address, align 8
  %length.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 2
  %sourceLength.value11 = load i32, ptr %sourceLength1, align 4
  store i32 %sourceLength.value11, ptr %length.address, align 4
  %index.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %this, i32 0, i32 3
  store i32 -1, ptr %index.address, align 4
  ret void

array.bounds.failure:                             ; preds = %entry
  %0 = call i32 @puts(ptr @array.bounds.message.1)
  call void @exit(i32 1)
  unreachable
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
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %if.body
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %if.body
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr, ptr @string.literal)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret void

error.continue:                                   ; preds = %managed.end
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %1 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %1, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret void

if.end2:                                          ; preds = %while.end, %if.end
  %buffer.address36 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value37 = load %absolute.array.int32.1, ptr %buffer.address36, align 8
  %array.data38 = extractvalue %absolute.array.int32.1 %buffer.value37, 0
  %array.owner39 = extractvalue %absolute.array.int32.1 %buffer.value37, 1
  %array.dimension40 = extractvalue %absolute.array.int32.1 %buffer.value37, 2
  %buffer.address41 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value42 = load %absolute.array.int32.1, ptr %buffer.address41, align 8
  %array.data43 = extractvalue %absolute.array.int32.1 %buffer.value42, 0
  %array.owner44 = extractvalue %absolute.array.int32.1 %buffer.value42, 1
  %array.dimension45 = extractvalue %absolute.array.int32.1 %buffer.value42, 2
  %_count.address46 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value47 = load i32, ptr %_count.address46, align 4
  %array.index.wide48 = sext i32 %_count.value47 to i64
  %array.index.valid49 = icmp ult i64 %array.index.wide48, %array.dimension45
  br i1 %array.index.valid49, label %array.bounds.success50, label %array.bounds.failure51, !prof !0

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
  %array.data9 = insertvalue %absolute.array.int32.1 undef, ptr %array.data6, 0
  %array.owner10 = insertvalue %absolute.array.int32.1 %array.data9, ptr %array.owner7, 1
  %array.dimension11 = insertvalue %absolute.array.int32.1 %array.owner10, i64 %array.dimension8, 2
  store i32 0, ptr %i, align 4
  br label %while.condition

while.condition:                                  ; preds = %array.bounds.success26, %if.body3
  %i.value = load i32, ptr %i, align 4
  %_count.address12 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value13 = load i32, ptr %_count.address12, align 4
  %less = icmp slt i32 %i.value, %_count.value13
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %i.value14 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value14 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension8
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %buffer.address30 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %copy.element.count = mul i64 1, %array.dimension8
  %copy.byte.count = mul i64 %copy.element.count, 4
  %copy.data = call ptr @malloc(i64 %copy.byte.count)
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %copy.data, ptr align 1 %array.data6, i64 %copy.byte.count, i1 false)
  %array.data31 = insertvalue %absolute.array.int32.1 undef, ptr %copy.data, 0
  %array.owner32 = insertvalue %absolute.array.int32.1 %array.data31, ptr %copy.data, 1
  %array.dimension33 = insertvalue %absolute.array.int32.1 %array.owner32, i64 %array.dimension8, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %buffer.address30, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %buffer.address30, align 8
  store %absolute.array.int32.1 %array.dimension33, ptr %buffer.address30, align 8
  %_capacity.address34 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 3
  %newCap.value35 = load i32, ptr %newCap, align 4
  store i32 %newCap.value35, ptr %_capacity.address34, align 4
  br label %if.end2

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data6, i32 %i.value14
  %buffer.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value = load %absolute.array.int32.1, ptr %buffer.address, align 8
  %array.data15 = extractvalue %absolute.array.int32.1 %buffer.value, 0
  %array.owner16 = extractvalue %absolute.array.int32.1 %buffer.value, 1
  %array.dimension17 = extractvalue %absolute.array.int32.1 %buffer.value, 2
  %buffer.address18 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value19 = load %absolute.array.int32.1, ptr %buffer.address18, align 8
  %array.data20 = extractvalue %absolute.array.int32.1 %buffer.value19, 0
  %array.owner21 = extractvalue %absolute.array.int32.1 %buffer.value19, 1
  %array.dimension22 = extractvalue %absolute.array.int32.1 %buffer.value19, 2
  %i.value23 = load i32, ptr %i, align 4
  %array.index.wide24 = sext i32 %i.value23 to i64
  %array.index.valid25 = icmp ult i64 %array.index.wide24, %array.dimension22
  br i1 %array.index.valid25, label %array.bounds.success26, label %array.bounds.failure27, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %2 = call i32 @puts(ptr @array.bounds.message.2)
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
  %3 = call i32 @puts(ptr @array.bounds.message.3)
  call void @exit(i32 1)
  unreachable

array.bounds.success50:                           ; preds = %if.end2
  %array.element.address52 = getelementptr inbounds i32, ptr %array.data43, i32 %_count.value47
  %move.value = load i32, ptr %element1, align 4
  call void @llvm.memset.p0.i64(ptr align 8 %element1, i8 0, i64 4, i1 false)
  store i32 %move.value, ptr %array.element.address52, align 4
  %_count.address53 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.address54 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value55 = load i32, ptr %_count.address54, align 4
  %add56 = add i32 %_count.value55, 1
  store i32 %add56, ptr %_count.address53, align 4
  ret void

array.bounds.failure51:                           ; preds = %if.end2
  %4 = call i32 @puts(ptr @array.bounds.message.4)
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
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %if.body
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %if.body
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr, ptr @string.literal.5)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i32 0

error.continue:                                   ; preds = %managed.end
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %1 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %1, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret i32 0

array.bounds.success:                             ; preds = %if.end
  %array.element.address = getelementptr inbounds i32, ptr %array.data5, i32 %index.value8
  %array.element = load i32, ptr %array.element.address, align 4
  ret i32 %array.element

array.bounds.failure:                             ; preds = %if.end
  %2 = call i32 @puts(ptr @array.bounds.message.6)
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
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %if.body
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %if.body
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr, ptr @string.literal.7)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret void

error.continue:                                   ; preds = %managed.end
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %1 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %1, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret void

if.end3:                                          ; preds = %if.end
  %buffer.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value = load %absolute.array.int32.1, ptr %buffer.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %buffer.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %buffer.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %buffer.value, 2
  %buffer.address32 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value33 = load %absolute.array.int32.1, ptr %buffer.address32, align 8
  %array.data34 = extractvalue %absolute.array.int32.1 %buffer.value33, 0
  %array.owner35 = extractvalue %absolute.array.int32.1 %buffer.value33, 1
  %array.dimension36 = extractvalue %absolute.array.int32.1 %buffer.value33, 2
  %index.value37 = load i32, ptr %index1, align 4
  %array.index.wide = sext i32 %index.value37 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension36
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

if.body5:                                         ; preds = %if.end
  %managed.handle6 = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle6, i64 6860172195720241041)
  %managed.id12 = trunc i64 %managed.handle6 to i32
  %2 = lshr i64 %managed.handle6, 32
  %managed.gen13 = trunc i64 %2 to i32
  %managed.slots.size14 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds15 = icmp ult i32 %managed.id12, %managed.slots.size14
  br i1 %managed.in.bounds15, label %managed.fast.load8, label %managed.slow10

managed.fast.check7:                              ; preds = %managed.fast.load8
  %managed.slot.ptr.field21 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr17, i32 0, i32 0
  %managed.ptr22 = load ptr, ptr %managed.slot.ptr.field21, align 8
  %managed.ptr.not.null23 = icmp ne ptr %managed.ptr22, null
  br i1 %managed.ptr.not.null23, label %managed.fast.valid9, label %managed.slow10

managed.fast.load8:                               ; preds = %if.body5
  %managed.slots.data16 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr17 = getelementptr %absolute.Slot, ptr %managed.slots.data16, i32 %managed.id12
  %managed.slot.gen.ptr18 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr17, i32 0, i32 1
  %managed.slot.gen19 = load i32, ptr %managed.slot.gen.ptr18, align 4
  %managed.gen.match20 = icmp eq i32 %managed.slot.gen19, %managed.gen13
  br i1 %managed.gen.match20, label %managed.fast.check7, label %managed.slow10

managed.fast.valid9:                              ; preds = %managed.fast.check7
  br label %managed.end11

managed.slow10:                                   ; preds = %managed.fast.check7, %managed.fast.load8, %if.body5
  %managed.slow.ptr24 = call ptr @absolute_managed_require(i64 %managed.handle6)
  br label %managed.end11

managed.end11:                                    ; preds = %managed.slow10, %managed.fast.valid9
  %managed.final.ptr25 = phi ptr [ %managed.ptr22, %managed.fast.valid9 ], [ %managed.slow.ptr24, %managed.slow10 ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr25, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address26 = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr25, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address26, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr25, ptr @string.literal.8)
  %error.pending27 = call i1 @absolute_error_pending()
  br i1 %error.pending27, label %error.propagate28, label %error.continue29

error.propagate28:                                ; preds = %managed.end11
  call void @absolute_managed_destroy(i64 %managed.handle6)
  ret void

error.continue29:                                 ; preds = %managed.end11
  %exception.dynamic.type30 = call i64 @absolute_managed_type(i64 %managed.handle6)
  %3 = icmp ne i64 %exception.dynamic.type30, 0
  %exception.effective.type31 = select i1 %3, i64 %exception.dynamic.type30, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle6, i64 %exception.effective.type31)
  ret void

array.bounds.success:                             ; preds = %if.end3
  %array.element.address = getelementptr inbounds i32, ptr %array.data34, i32 %index.value37
  %move.value = load i32, ptr %element2, align 4
  call void @llvm.memset.p0.i64(ptr align 8 %element2, i8 0, i64 4, i1 false)
  store i32 %move.value, ptr %array.element.address, align 4
  ret void

array.bounds.failure:                             ; preds = %if.end3
  %4 = call i32 @puts(ptr @array.bounds.message.9)
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
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %if.body
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %if.body
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr, ptr @string.literal.10)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret void

error.continue:                                   ; preds = %managed.end
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %1 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %1, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret void

if.end2:                                          ; preds = %if.end
  %index.value31 = load i32, ptr %index1, align 4
  store i32 %index.value31, ptr %i, align 4
  br label %while.condition

if.body4:                                         ; preds = %if.end
  %managed.handle5 = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle5, i64 6860172195720241041)
  %managed.id11 = trunc i64 %managed.handle5 to i32
  %2 = lshr i64 %managed.handle5, 32
  %managed.gen12 = trunc i64 %2 to i32
  %managed.slots.size13 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds14 = icmp ult i32 %managed.id11, %managed.slots.size13
  br i1 %managed.in.bounds14, label %managed.fast.load7, label %managed.slow9

managed.fast.check6:                              ; preds = %managed.fast.load7
  %managed.slot.ptr.field20 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr16, i32 0, i32 0
  %managed.ptr21 = load ptr, ptr %managed.slot.ptr.field20, align 8
  %managed.ptr.not.null22 = icmp ne ptr %managed.ptr21, null
  br i1 %managed.ptr.not.null22, label %managed.fast.valid8, label %managed.slow9

managed.fast.load7:                               ; preds = %if.body4
  %managed.slots.data15 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr16 = getelementptr %absolute.Slot, ptr %managed.slots.data15, i32 %managed.id11
  %managed.slot.gen.ptr17 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr16, i32 0, i32 1
  %managed.slot.gen18 = load i32, ptr %managed.slot.gen.ptr17, align 4
  %managed.gen.match19 = icmp eq i32 %managed.slot.gen18, %managed.gen12
  br i1 %managed.gen.match19, label %managed.fast.check6, label %managed.slow9

managed.fast.valid8:                              ; preds = %managed.fast.check6
  br label %managed.end10

managed.slow9:                                    ; preds = %managed.fast.check6, %managed.fast.load7, %if.body4
  %managed.slow.ptr23 = call ptr @absolute_managed_require(i64 %managed.handle5)
  br label %managed.end10

managed.end10:                                    ; preds = %managed.slow9, %managed.fast.valid8
  %managed.final.ptr24 = phi ptr [ %managed.ptr21, %managed.fast.valid8 ], [ %managed.slow.ptr23, %managed.slow9 ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr24, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address25 = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr24, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address25, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr24, ptr @string.literal.11)
  %error.pending26 = call i1 @absolute_error_pending()
  br i1 %error.pending26, label %error.propagate27, label %error.continue28

error.propagate27:                                ; preds = %managed.end10
  call void @absolute_managed_destroy(i64 %managed.handle5)
  ret void

error.continue28:                                 ; preds = %managed.end10
  %exception.dynamic.type29 = call i64 @absolute_managed_type(i64 %managed.handle5)
  %3 = icmp ne i64 %exception.dynamic.type29, 0
  %exception.effective.type30 = select i1 %3, i64 %exception.dynamic.type29, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle5, i64 %exception.effective.type30)
  ret void

while.condition:                                  ; preds = %array.bounds.success54, %if.end2
  %i.value = load i32, ptr %i, align 4
  %_count.address32 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value33 = load i32, ptr %_count.address32, align 4
  %sub = sub i32 %_count.value33, 1
  %less34 = icmp slt i32 %i.value, %sub
  br i1 %less34, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %buffer.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value = load %absolute.array.int32.1, ptr %buffer.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %buffer.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %buffer.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %buffer.value, 2
  %buffer.address35 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value36 = load %absolute.array.int32.1, ptr %buffer.address35, align 8
  %array.data37 = extractvalue %absolute.array.int32.1 %buffer.value36, 0
  %array.owner38 = extractvalue %absolute.array.int32.1 %buffer.value36, 1
  %array.dimension39 = extractvalue %absolute.array.int32.1 %buffer.value36, 2
  %i.value40 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value40 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension39
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %_count.address59 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.address60 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value61 = load i32, ptr %_count.address60, align 4
  %sub62 = sub i32 %_count.value61, 1
  store i32 %sub62, ptr %_count.address59, align 4
  ret void

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data37, i32 %i.value40
  %buffer.address41 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value42 = load %absolute.array.int32.1, ptr %buffer.address41, align 8
  %array.data43 = extractvalue %absolute.array.int32.1 %buffer.value42, 0
  %array.owner44 = extractvalue %absolute.array.int32.1 %buffer.value42, 1
  %array.dimension45 = extractvalue %absolute.array.int32.1 %buffer.value42, 2
  %buffer.address46 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value47 = load %absolute.array.int32.1, ptr %buffer.address46, align 8
  %array.data48 = extractvalue %absolute.array.int32.1 %buffer.value47, 0
  %array.owner49 = extractvalue %absolute.array.int32.1 %buffer.value47, 1
  %array.dimension50 = extractvalue %absolute.array.int32.1 %buffer.value47, 2
  %i.value51 = load i32, ptr %i, align 4
  %add = add i32 %i.value51, 1
  %array.index.wide52 = sext i32 %add to i64
  %array.index.valid53 = icmp ult i64 %array.index.wide52, %array.dimension50
  br i1 %array.index.valid53, label %array.bounds.success54, label %array.bounds.failure55, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %4 = call i32 @puts(ptr @array.bounds.message.12)
  call void @exit(i32 1)
  unreachable

array.bounds.success54:                           ; preds = %array.bounds.success
  %array.element.address56 = getelementptr inbounds i32, ptr %array.data48, i32 %add
  %array.element = load i32, ptr %array.element.address56, align 4
  store i32 %array.element, ptr %array.element.address, align 4
  %i.value57 = load i32, ptr %i, align 4
  %add58 = add i32 %i.value57, 1
  store i32 %add58, ptr %i, align 4
  br label %while.condition

array.bounds.failure55:                           ; preds = %array.bounds.success
  %5 = call i32 @puts(ptr @array.bounds.message.13)
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
  %managed.id8 = trunc i64 %managed.handle2 to i32
  %0 = lshr i64 %managed.handle2, 32
  %managed.gen9 = trunc i64 %0 to i32
  %managed.slots.size10 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds11 = icmp ult i32 %managed.id8, %managed.slots.size10
  br i1 %managed.in.bounds11, label %managed.fast.load4, label %managed.slow6

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.id = trunc i64 %managed.handle to i32
  %1 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %1 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %if.body
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %if.body
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr, ptr @string.literal.14)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i64 0

error.continue:                                   ; preds = %managed.end
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %2 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %2, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret i64 0

managed.fast.check3:                              ; preds = %managed.fast.load4
  %managed.slot.ptr.field17 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr13, i32 0, i32 0
  %managed.ptr18 = load ptr, ptr %managed.slot.ptr.field17, align 8
  %managed.ptr.not.null19 = icmp ne ptr %managed.ptr18, null
  br i1 %managed.ptr.not.null19, label %managed.fast.valid5, label %managed.slow6

managed.fast.load4:                               ; preds = %if.end
  %managed.slots.data12 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr13 = getelementptr %absolute.Slot, ptr %managed.slots.data12, i32 %managed.id8
  %managed.slot.gen.ptr14 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr13, i32 0, i32 1
  %managed.slot.gen15 = load i32, ptr %managed.slot.gen.ptr14, align 4
  %managed.gen.match16 = icmp eq i32 %managed.slot.gen15, %managed.gen9
  br i1 %managed.gen.match16, label %managed.fast.check3, label %managed.slow6

managed.fast.valid5:                              ; preds = %managed.fast.check3
  br label %managed.end7

managed.slow6:                                    ; preds = %managed.fast.check3, %managed.fast.load4, %if.end
  %managed.slow.ptr20 = call ptr @absolute_managed_require(i64 %managed.handle2)
  br label %managed.end7

managed.end7:                                     ; preds = %managed.slow6, %managed.fast.valid5
  %managed.final.ptr21 = phi ptr [ %managed.ptr18, %managed.fast.valid5 ], [ %managed.slow.ptr20, %managed.slow6 ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr21, i8 0, i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.Vector<int32>", ptr null, i32 1) to i64), i1 false)
  %vtable.address22 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %managed.final.ptr21, i32 0, i32 0
  store ptr @"std.collections.Vector<int32>.__vtable", ptr %vtable.address22, align 8
  call void @"std.collections.Vector<int32>.__ctor"(ptr %managed.final.ptr21)
  %error.pending23 = call i1 @absolute_error_pending()
  br i1 %error.pending23, label %error.propagate24, label %error.continue25

error.propagate24:                                ; preds = %managed.end7
  call void @absolute_managed_destroy(i64 %managed.handle2)
  %cleanup.handle = load i64, ptr %result, align 4
  %managed.id31 = trunc i64 %cleanup.handle to i32
  %3 = lshr i64 %cleanup.handle, 32
  %managed.gen32 = trunc i64 %3 to i32
  %managed.slots.size33 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds34 = icmp ult i32 %managed.id31, %managed.slots.size33
  br i1 %managed.in.bounds34, label %managed.fast.load27, label %managed.slow29

error.continue25:                                 ; preds = %managed.end7
  store i64 %managed.handle2, ptr %result, align 4
  store ptr %managed.final.ptr21, ptr %result.cached.pointee, align 8
  store i32 0, ptr %i, align 4
  br label %while.condition

managed.fast.check26:                             ; preds = %managed.fast.load27
  %managed.slot.ptr.field40 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr36, i32 0, i32 0
  %managed.ptr41 = load ptr, ptr %managed.slot.ptr.field40, align 8
  %managed.ptr.not.null42 = icmp ne ptr %managed.ptr41, null
  br i1 %managed.ptr.not.null42, label %managed.fast.valid28, label %managed.slow29

managed.fast.load27:                              ; preds = %error.propagate24
  %managed.slots.data35 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr36 = getelementptr %absolute.Slot, ptr %managed.slots.data35, i32 %managed.id31
  %managed.slot.gen.ptr37 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr36, i32 0, i32 1
  %managed.slot.gen38 = load i32, ptr %managed.slot.gen.ptr37, align 4
  %managed.gen.match39 = icmp eq i32 %managed.slot.gen38, %managed.gen32
  br i1 %managed.gen.match39, label %managed.fast.check26, label %managed.slow29

managed.fast.valid28:                             ; preds = %managed.fast.check26
  br label %managed.end30

managed.slow29:                                   ; preds = %managed.fast.check26, %managed.fast.load27, %error.propagate24
  %managed.slow.ptr43 = call ptr @absolute_managed_get(i64 %cleanup.handle)
  br label %managed.end30

managed.end30:                                    ; preds = %managed.slow29, %managed.fast.valid28
  %managed.final.ptr44 = phi ptr [ %managed.ptr41, %managed.fast.valid28 ], [ %managed.slow.ptr43, %managed.slow29 ]
  %aggregate.present = icmp ne ptr %managed.final.ptr44, null
  br i1 %aggregate.present, label %aggregate.cleanup, label %aggregate.cleanup.end

aggregate.cleanup:                                ; preds = %managed.end30
  %aggregate.vtable = load ptr, ptr %managed.final.ptr44, align 8
  %aggregate.destroy.slot = getelementptr ptr, ptr %aggregate.vtable, i64 0
  %aggregate.destroy = load ptr, ptr %aggregate.destroy.slot, align 8
  call void %aggregate.destroy(ptr %managed.final.ptr44)
  br label %aggregate.cleanup.end

aggregate.cleanup.end:                            ; preds = %aggregate.cleanup, %managed.end30
  call void @absolute_managed_destroy(i64 %cleanup.handle)
  store i64 0, ptr %result, align 4
  ret i64 0

while.condition:                                  ; preds = %error.continue54, %error.continue25
  %i.value = load i32, ptr %i, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %less = icmp slt i32 %i.value, %_count.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %result.value = load i64, ptr %result, align 4
  %result.cached.pointee45 = load ptr, ptr %result.cached.pointee, align 8
  %buffer.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value = load %absolute.array.int32.1, ptr %buffer.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %buffer.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %buffer.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %buffer.value, 2
  %buffer.address46 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value47 = load %absolute.array.int32.1, ptr %buffer.address46, align 8
  %array.data48 = extractvalue %absolute.array.int32.1 %buffer.value47, 0
  %array.owner49 = extractvalue %absolute.array.int32.1 %buffer.value47, 1
  %array.dimension50 = extractvalue %absolute.array.int32.1 %buffer.value47, 2
  %i.value51 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value51 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension50
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %result.value82 = load i64, ptr %result, align 4
  ret i64 %result.value82

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data48, i32 %i.value51
  %array.element = load i32, ptr %array.element.address, align 4
  call void @"std.collections.Vector<int32>.push$int32"(ptr %result.cached.pointee45, i32 %array.element)
  %error.pending52 = call i1 @absolute_error_pending()
  br i1 %error.pending52, label %error.propagate53, label %error.continue54

array.bounds.failure:                             ; preds = %while.body
  %4 = call i32 @puts(ptr @array.bounds.message.15)
  call void @exit(i32 1)
  unreachable

error.propagate53:                                ; preds = %array.bounds.success
  %cleanup.handle55 = load i64, ptr %result, align 4
  %managed.id61 = trunc i64 %cleanup.handle55 to i32
  %5 = lshr i64 %cleanup.handle55, 32
  %managed.gen62 = trunc i64 %5 to i32
  %managed.slots.size63 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds64 = icmp ult i32 %managed.id61, %managed.slots.size63
  br i1 %managed.in.bounds64, label %managed.fast.load57, label %managed.slow59

error.continue54:                                 ; preds = %array.bounds.success
  %i.value81 = load i32, ptr %i, align 4
  %add = add i32 %i.value81, 1
  store i32 %add, ptr %i, align 4
  br label %while.condition

managed.fast.check56:                             ; preds = %managed.fast.load57
  %managed.slot.ptr.field70 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr66, i32 0, i32 0
  %managed.ptr71 = load ptr, ptr %managed.slot.ptr.field70, align 8
  %managed.ptr.not.null72 = icmp ne ptr %managed.ptr71, null
  br i1 %managed.ptr.not.null72, label %managed.fast.valid58, label %managed.slow59

managed.fast.load57:                              ; preds = %error.propagate53
  %managed.slots.data65 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr66 = getelementptr %absolute.Slot, ptr %managed.slots.data65, i32 %managed.id61
  %managed.slot.gen.ptr67 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr66, i32 0, i32 1
  %managed.slot.gen68 = load i32, ptr %managed.slot.gen.ptr67, align 4
  %managed.gen.match69 = icmp eq i32 %managed.slot.gen68, %managed.gen62
  br i1 %managed.gen.match69, label %managed.fast.check56, label %managed.slow59

managed.fast.valid58:                             ; preds = %managed.fast.check56
  br label %managed.end60

managed.slow59:                                   ; preds = %managed.fast.check56, %managed.fast.load57, %error.propagate53
  %managed.slow.ptr73 = call ptr @absolute_managed_get(i64 %cleanup.handle55)
  br label %managed.end60

managed.end60:                                    ; preds = %managed.slow59, %managed.fast.valid58
  %managed.final.ptr74 = phi ptr [ %managed.ptr71, %managed.fast.valid58 ], [ %managed.slow.ptr73, %managed.slow59 ]
  %aggregate.present77 = icmp ne ptr %managed.final.ptr74, null
  br i1 %aggregate.present77, label %aggregate.cleanup75, label %aggregate.cleanup.end76

aggregate.cleanup75:                              ; preds = %managed.end60
  %aggregate.vtable78 = load ptr, ptr %managed.final.ptr74, align 8
  %aggregate.destroy.slot79 = getelementptr ptr, ptr %aggregate.vtable78, i64 0
  %aggregate.destroy80 = load ptr, ptr %aggregate.destroy.slot79, align 8
  call void %aggregate.destroy80(ptr %managed.final.ptr74)
  br label %aggregate.cleanup.end76

aggregate.cleanup.end76:                          ; preds = %aggregate.cleanup75, %managed.end60
  call void @absolute_managed_destroy(i64 %cleanup.handle55)
  store i64 0, ptr %result, align 4
  store ptr null, ptr %result.cached.pointee, align 8
  ret i64 0
}

define void @"std.collections.VectorBuilder<int32>.__ctor$int32_5B_5D$int32"(ptr %this, %absolute.array.int32.1 %initialSource, i32 %initialCount) {
entry:
  %i = alloca i32, align 4
  %initialCount1 = alloca i32, align 4
  %array.data = extractvalue %absolute.array.int32.1 %initialSource, 0
  %array.owner = extractvalue %absolute.array.int32.1 %initialSource, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %initialSource, 2
  store i32 %initialCount, ptr %initialCount1, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %initialCount.value = load i32, ptr %initialCount1, align 4
  store i32 %initialCount.value, ptr %_count.address, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 3
  %initialCount.value2 = load i32, ptr %initialCount1, align 4
  %greater = icmp sgt i32 %initialCount.value2, 4
  br i1 %greater, label %ternary.true, label %ternary.false

ternary.true:                                     ; preds = %entry
  %initialCount.value3 = load i32, ptr %initialCount1, align 4
  br label %ternary.end

ternary.false:                                    ; preds = %entry
  br label %ternary.end

ternary.end:                                      ; preds = %ternary.false, %ternary.true
  %ternary.result = phi i32 [ %initialCount.value3, %ternary.true ], [ 4, %ternary.false ]
  store i32 %ternary.result, ptr %_capacity.address, align 4
  %buffer.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %_capacity.address4 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 3
  %_capacity.value = load i32, ptr %_capacity.address4, align 4
  %int.cast = sext i32 %_capacity.value to i64
  %array.alloc.bytes = mul i64 %int.cast, 4
  %array.data.alloc = call ptr @malloc(i64 %array.alloc.bytes)
  %array.data5 = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc, 0
  %array.owner6 = insertvalue %absolute.array.int32.1 %array.data5, ptr %array.data.alloc, 1
  %array.dimension7 = insertvalue %absolute.array.int32.1 %array.owner6, i64 %int.cast, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %buffer.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %buffer.address, align 8
  store %absolute.array.int32.1 %array.dimension7, ptr %buffer.address, align 8
  store i32 0, ptr %i, align 4
  br label %while.condition

while.condition:                                  ; preds = %array.bounds.success22, %ternary.end
  %i.value = load i32, ptr %i, align 4
  %_count.address8 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address8, align 4
  %less = icmp slt i32 %i.value, %_count.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %buffer.address9 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value = load %absolute.array.int32.1, ptr %buffer.address9, align 8
  %array.data10 = extractvalue %absolute.array.int32.1 %buffer.value, 0
  %array.owner11 = extractvalue %absolute.array.int32.1 %buffer.value, 1
  %array.dimension12 = extractvalue %absolute.array.int32.1 %buffer.value, 2
  %buffer.address13 = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 1
  %buffer.value14 = load %absolute.array.int32.1, ptr %buffer.address13, align 8
  %array.data15 = extractvalue %absolute.array.int32.1 %buffer.value14, 0
  %array.owner16 = extractvalue %absolute.array.int32.1 %buffer.value14, 1
  %array.dimension17 = extractvalue %absolute.array.int32.1 %buffer.value14, 2
  %i.value18 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value18 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension17
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %_finished.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %this, i32 0, i32 4
  store i1 false, ptr %_finished.address, align 1
  ret void

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data15, i32 %i.value18
  %i.value19 = load i32, ptr %i, align 4
  %array.index.wide20 = sext i32 %i.value19 to i64
  %array.index.valid21 = icmp ult i64 %array.index.wide20, %array.dimension
  br i1 %array.index.valid21, label %array.bounds.success22, label %array.bounds.failure23, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.16)
  call void @exit(i32 1)
  unreachable

array.bounds.success22:                           ; preds = %array.bounds.success
  %array.element.address24 = getelementptr inbounds i32, ptr %array.data, i32 %i.value19
  %array.element = load i32, ptr %array.element.address24, align 4
  store i32 %array.element, ptr %array.element.address, align 4
  %i.value25 = load i32, ptr %i, align 4
  %add = add i32 %i.value25, 1
  store i32 %add, ptr %i, align 4
  br label %while.condition

array.bounds.failure23:                           ; preds = %array.bounds.success
  %1 = call i32 @puts(ptr @array.bounds.message.17)
  call void @exit(i32 1)
  unreachable
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

define void @"std.collections.Vector<int32>.push$int32"(ptr %this, i32 %element) {
entry:
  %i = alloca i32, align 4
  %newCap = alloca i32, align 4
  %element1 = alloca i32, align 4
  store i32 %element, ptr %element1, align 4
  call void @"std.collections.Vector<int32>.ensureUnshared"(ptr %this)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  ret void

error.continue:                                   ; preds = %entry
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 3
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %equal = icmp eq i32 %_count.value, %_capacity.value
  br i1 %equal, label %if.body, label %if.end

if.end:                                           ; preds = %while.end, %error.continue
  %items.address34 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value35 = load %absolute.array.int32.1, ptr %items.address34, align 8
  %array.data36 = extractvalue %absolute.array.int32.1 %items.value35, 0
  %array.owner37 = extractvalue %absolute.array.int32.1 %items.value35, 1
  %array.dimension38 = extractvalue %absolute.array.int32.1 %items.value35, 2
  %items.address39 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value40 = load %absolute.array.int32.1, ptr %items.address39, align 8
  %array.data41 = extractvalue %absolute.array.int32.1 %items.value40, 0
  %array.owner42 = extractvalue %absolute.array.int32.1 %items.value40, 1
  %array.dimension43 = extractvalue %absolute.array.int32.1 %items.value40, 2
  %_count.address44 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value45 = load i32, ptr %_count.address44, align 4
  %array.index.wide46 = sext i32 %_count.value45 to i64
  %array.index.valid47 = icmp ult i64 %array.index.wide46, %array.dimension43
  br i1 %array.index.valid47, label %array.bounds.success48, label %array.bounds.failure49, !prof !0

if.body:                                          ; preds = %error.continue
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
  %array.data7 = insertvalue %absolute.array.int32.1 undef, ptr %array.data4, 0
  %array.owner8 = insertvalue %absolute.array.int32.1 %array.data7, ptr %array.owner5, 1
  %array.dimension9 = insertvalue %absolute.array.int32.1 %array.owner8, i64 %array.dimension6, 2
  store i32 0, ptr %i, align 4
  br label %while.condition

while.condition:                                  ; preds = %array.bounds.success24, %if.body
  %i.value = load i32, ptr %i, align 4
  %_count.address10 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value11 = load i32, ptr %_count.address10, align 4
  %less = icmp slt i32 %i.value, %_count.value11
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %i.value12 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value12 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension6
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %items.address28 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %copy.element.count = mul i64 1, %array.dimension6
  %copy.byte.count = mul i64 %copy.element.count, 4
  %copy.data = call ptr @malloc(i64 %copy.byte.count)
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %copy.data, ptr align 1 %array.data4, i64 %copy.byte.count, i1 false)
  %array.data29 = insertvalue %absolute.array.int32.1 undef, ptr %copy.data, 0
  %array.owner30 = insertvalue %absolute.array.int32.1 %array.data29, ptr %copy.data, 1
  %array.dimension31 = insertvalue %absolute.array.int32.1 %array.owner30, i64 %array.dimension6, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %items.address28, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %items.address28, align 8
  store %absolute.array.int32.1 %array.dimension31, ptr %items.address28, align 8
  %_capacity.address32 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 3
  %newCap.value33 = load i32, ptr %newCap, align 4
  store i32 %newCap.value33, ptr %_capacity.address32, align 4
  br label %if.end

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data4, i32 %i.value12
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data13 = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner14 = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension15 = extractvalue %absolute.array.int32.1 %items.value, 2
  %items.address16 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value17 = load %absolute.array.int32.1, ptr %items.address16, align 8
  %array.data18 = extractvalue %absolute.array.int32.1 %items.value17, 0
  %array.owner19 = extractvalue %absolute.array.int32.1 %items.value17, 1
  %array.dimension20 = extractvalue %absolute.array.int32.1 %items.value17, 2
  %i.value21 = load i32, ptr %i, align 4
  %array.index.wide22 = sext i32 %i.value21 to i64
  %array.index.valid23 = icmp ult i64 %array.index.wide22, %array.dimension20
  br i1 %array.index.valid23, label %array.bounds.success24, label %array.bounds.failure25, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.18)
  call void @exit(i32 1)
  unreachable

array.bounds.success24:                           ; preds = %array.bounds.success
  %array.element.address26 = getelementptr inbounds i32, ptr %array.data18, i32 %i.value21
  %array.element = load i32, ptr %array.element.address26, align 4
  store i32 %array.element, ptr %array.element.address, align 4
  %i.value27 = load i32, ptr %i, align 4
  %add = add i32 %i.value27, 1
  store i32 %add, ptr %i, align 4
  br label %while.condition

array.bounds.failure25:                           ; preds = %array.bounds.success
  %1 = call i32 @puts(ptr @array.bounds.message.19)
  call void @exit(i32 1)
  unreachable

array.bounds.success48:                           ; preds = %if.end
  %array.element.address50 = getelementptr inbounds i32, ptr %array.data41, i32 %_count.value45
  %move.value = load i32, ptr %element1, align 4
  call void @llvm.memset.p0.i64(ptr align 8 %element1, i8 0, i64 4, i1 false)
  store i32 %move.value, ptr %array.element.address50, align 4
  %_count.address51 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.address52 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value53 = load i32, ptr %_count.address52, align 4
  %add54 = add i32 %_count.value53, 1
  store i32 %add54, ptr %_count.address51, align 4
  ret void

array.bounds.failure49:                           ; preds = %if.end
  %2 = call i32 @puts(ptr @array.bounds.message.20)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.Vector<int32>.ensureUnshared"(ptr %this) {
entry:
  %_isShared.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 4
  %_isShared.value = load i1, ptr %_isShared.address, align 1
  br i1 %_isShared.value, label %if.body, label %if.end

if.end:                                           ; preds = %if.body, %entry
  ret void

if.body:                                          ; preds = %entry
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.address1 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address1, align 8
  %array.data = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %items.value, 2
  %copy.element.count = mul i64 1, %array.dimension
  %copy.byte.count = mul i64 %copy.element.count, 4
  %copy.data = call ptr @malloc(i64 %copy.byte.count)
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %copy.data, ptr align 1 %array.data, i64 %copy.byte.count, i1 false)
  %array.data2 = insertvalue %absolute.array.int32.1 undef, ptr %copy.data, 0
  %array.owner3 = insertvalue %absolute.array.int32.1 %array.data2, ptr %copy.data, 1
  %array.dimension4 = insertvalue %absolute.array.int32.1 %array.owner3, i64 %array.dimension, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %items.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %items.address, align 8
  store %absolute.array.int32.1 %array.dimension4, ptr %items.address, align 8
  %_isShared.address5 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 4
  store i1 false, ptr %_isShared.address5, align 1
  br label %if.end
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
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %if.body
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %if.body
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr, ptr @string.literal.21)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i32 0

error.continue:                                   ; preds = %managed.end
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %1 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %1, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret i32 0

array.bounds.success:                             ; preds = %if.end
  %array.element.address = getelementptr inbounds i32, ptr %array.data3, i32 %sub
  %array.element = load i32, ptr %array.element.address, align 4
  ret i32 %array.element

array.bounds.failure:                             ; preds = %if.end
  %2 = call i32 @puts(ptr @array.bounds.message.22)
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
  call void @"std.collections.Vector<int32>.ensureUnshared"(ptr %this)
  %error.pending1 = call i1 @absolute_error_pending()
  br i1 %error.pending1, label %error.propagate2, label %error.continue3

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %if.body
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %if.body
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr, ptr @string.literal.23)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i32 0

error.continue:                                   ; preds = %managed.end
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %1 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %1, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret i32 0

error.propagate2:                                 ; preds = %if.end
  ret i32 0

error.continue3:                                  ; preds = %if.end
  %_count.address4 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.address5 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value6 = load i32, ptr %_count.address5, align 4
  %sub = sub i32 %_count.value6, 1
  store i32 %sub, ptr %_count.address4, align 4
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
  %_count.address12 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value13 = load i32, ptr %_count.address12, align 4
  %array.index.wide = sext i32 %_count.value13 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension11
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

array.bounds.success:                             ; preds = %error.continue3
  %array.element.address = getelementptr inbounds i32, ptr %array.data9, i32 %_count.value13
  %array.element = load i32, ptr %array.element.address, align 4
  ret i32 %array.element

array.bounds.failure:                             ; preds = %error.continue3
  %2 = call i32 @puts(ptr @array.bounds.message.24)
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
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %if.body
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %if.body
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr, ptr @string.literal.25)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i32 0

error.continue:                                   ; preds = %managed.end
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %1 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %1, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret i32 0

array.bounds.success:                             ; preds = %if.end
  %array.element.address = getelementptr inbounds i32, ptr %array.data3, i32 0
  %array.element = load i32, ptr %array.element.address, align 4
  ret i32 %array.element

array.bounds.failure:                             ; preds = %if.end
  %2 = call i32 @puts(ptr @array.bounds.message.26)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.Vector<int32>.clear"(ptr %this) {
entry:
  call void @"std.collections.Vector<int32>.ensureUnshared"(ptr %this)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  ret void

error.continue:                                   ; preds = %entry
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  store i32 0, ptr %_count.address, align 4
  ret void
}

define i64 @"std.collections.Vector<int32>.builder"(ptr %this) {
entry:
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.VectorBuilder<int32>", ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 620979060937029473)
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %entry
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %entry
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.VectorBuilder<int32>", ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %"absolute.class.std.collections.VectorBuilder<int32>", ptr %managed.final.ptr, i32 0, i32 0
  store ptr @"std.collections.VectorBuilder<int32>.__vtable", ptr %vtable.address, align 8
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  call void @"std.collections.VectorBuilder<int32>.__ctor$int32_5B_5D$int32"(ptr %managed.final.ptr, %absolute.array.int32.1 %items.value, i32 %_count.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i64 0

error.continue:                                   ; preds = %managed.end
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
  call void @"std.collections.Vector<int32>.ensureUnshared"(ptr %this)
  %error.pending3 = call i1 @absolute_error_pending()
  br i1 %error.pending3, label %error.propagate4, label %error.continue5

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %if.body
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %if.body
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr, ptr @string.literal.27)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret void

error.continue:                                   ; preds = %managed.end
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %1 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %1, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret void

error.propagate4:                                 ; preds = %if.end
  ret void

error.continue5:                                  ; preds = %if.end
  %index.value6 = load i32, ptr %index1, align 4
  store i32 %index.value6, ptr %i, align 4
  br label %while.condition

while.condition:                                  ; preds = %array.bounds.success29, %error.continue5
  %i.value = load i32, ptr %i, align 4
  %_count.address7 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value8 = load i32, ptr %_count.address7, align 4
  %sub = sub i32 %_count.value8, 1
  %less9 = icmp slt i32 %i.value, %sub
  br i1 %less9, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %items.value, 2
  %items.address10 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value11 = load %absolute.array.int32.1, ptr %items.address10, align 8
  %array.data12 = extractvalue %absolute.array.int32.1 %items.value11, 0
  %array.owner13 = extractvalue %absolute.array.int32.1 %items.value11, 1
  %array.dimension14 = extractvalue %absolute.array.int32.1 %items.value11, 2
  %i.value15 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value15 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension14
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %_count.address34 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.address35 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value36 = load i32, ptr %_count.address35, align 4
  %sub37 = sub i32 %_count.value36, 1
  store i32 %sub37, ptr %_count.address34, align 4
  ret void

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data12, i32 %i.value15
  %items.address16 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value17 = load %absolute.array.int32.1, ptr %items.address16, align 8
  %array.data18 = extractvalue %absolute.array.int32.1 %items.value17, 0
  %array.owner19 = extractvalue %absolute.array.int32.1 %items.value17, 1
  %array.dimension20 = extractvalue %absolute.array.int32.1 %items.value17, 2
  %items.address21 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value22 = load %absolute.array.int32.1, ptr %items.address21, align 8
  %array.data23 = extractvalue %absolute.array.int32.1 %items.value22, 0
  %array.owner24 = extractvalue %absolute.array.int32.1 %items.value22, 1
  %array.dimension25 = extractvalue %absolute.array.int32.1 %items.value22, 2
  %i.value26 = load i32, ptr %i, align 4
  %add = add i32 %i.value26, 1
  %array.index.wide27 = sext i32 %add to i64
  %array.index.valid28 = icmp ult i64 %array.index.wide27, %array.dimension25
  br i1 %array.index.valid28, label %array.bounds.success29, label %array.bounds.failure30, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %2 = call i32 @puts(ptr @array.bounds.message.28)
  call void @exit(i32 1)
  unreachable

array.bounds.success29:                           ; preds = %array.bounds.success
  %array.element.address31 = getelementptr inbounds i32, ptr %array.data23, i32 %add
  %array.element = load i32, ptr %array.element.address31, align 4
  store i32 %array.element, ptr %array.element.address, align 4
  %i.value32 = load i32, ptr %i, align 4
  %add33 = add i32 %i.value32, 1
  store i32 %add33, ptr %i, align 4
  br label %while.condition

array.bounds.failure30:                           ; preds = %array.bounds.success
  %3 = call i32 @puts(ptr @array.bounds.message.29)
  call void @exit(i32 1)
  unreachable
}

define i32 @"std.collections.Vector<int32>.__absolute_indexer_get$int32"(ptr %this, i32 %index) {
entry:
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
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %items.value, 2
  %items.address3 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value4 = load %absolute.array.int32.1, ptr %items.address3, align 8
  %array.data5 = extractvalue %absolute.array.int32.1 %items.value4, 0
  %array.owner6 = extractvalue %absolute.array.int32.1 %items.value4, 1
  %array.dimension7 = extractvalue %absolute.array.int32.1 %items.value4, 2
  %index.value8 = load i32, ptr %index1, align 4
  %array.index.wide = sext i32 %index.value8 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension7
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %if.body
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %if.body
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr, ptr @string.literal.30)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i32 0

error.continue:                                   ; preds = %managed.end
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %1 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %1, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret i32 0

array.bounds.success:                             ; preds = %if.end
  %array.element.address = getelementptr inbounds i32, ptr %array.data5, i32 %index.value8
  %array.element = load i32, ptr %array.element.address, align 4
  ret i32 %array.element

array.bounds.failure:                             ; preds = %if.end
  %2 = call i32 @puts(ptr @array.bounds.message.31)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.Vector<int32>.__absolute_indexer_set$int32$int32"(ptr %this, i32 %index, i32 %value) {
entry:
  %value2 = alloca i32, align 4
  %index1 = alloca i32, align 4
  store i32 %index, ptr %index1, align 4
  store i32 %value, ptr %value2, align 4
  %index.value = load i32, ptr %index1, align 4
  %less = icmp slt i32 %index.value, 0
  %index.value3 = load i32, ptr %index1, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  %greater.equal = icmp sge i32 %index.value3, %_count.value
  %logical.or = or i1 %less, %greater.equal
  br i1 %logical.or, label %if.body, label %if.end

if.end:                                           ; preds = %entry
  call void @"std.collections.Vector<int32>.ensureUnshared"(ptr %this)
  %error.pending4 = call i1 @absolute_error_pending()
  br i1 %error.pending4, label %error.propagate5, label %error.continue6

if.body:                                          ; preds = %entry
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 6860172195720241041)
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %if.body
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %if.body
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%absolute.class.Error, ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %absolute.class.Error, ptr %managed.final.ptr, i32 0, i32 0
  store ptr @Error.__vtable, ptr %vtable.address, align 8
  call void @"Error.__ctor$string"(ptr %managed.final.ptr, ptr @string.literal.32)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret void

error.continue:                                   ; preds = %managed.end
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %1 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %1, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret void

error.propagate5:                                 ; preds = %if.end
  ret void

error.continue6:                                  ; preds = %if.end
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
  %index.value12 = load i32, ptr %index1, align 4
  %array.index.wide = sext i32 %index.value12 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension11
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

array.bounds.success:                             ; preds = %error.continue6
  %array.element.address = getelementptr inbounds i32, ptr %array.data9, i32 %index.value12
  %move.value = load i32, ptr %value2, align 4
  call void @llvm.memset.p0.i64(ptr align 8 %value2, i8 0, i64 4, i1 false)
  store i32 %move.value, ptr %array.element.address, align 4
  ret void

array.bounds.failure:                             ; preds = %error.continue6
  %2 = call i32 @puts(ptr @array.bounds.message.33)
  call void @exit(i32 1)
  unreachable
}

define i64 @"std.collections.Vector<int32>.iterate"(ptr %this) {
entry:
  %_isShared.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 4
  store i1 true, ptr %_isShared.address, align 1
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.VectorIterator<int32>", ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 -1630790955709830350)
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %entry
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %entry
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.VectorIterator<int32>", ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %"absolute.class.std.collections.VectorIterator<int32>", ptr %managed.final.ptr, i32 0, i32 0
  store ptr @"std.collections.VectorIterator<int32>.__vtable", ptr %vtable.address, align 8
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value = load i32, ptr %_count.address, align 4
  call void @"std.collections.VectorIterator<int32>.__ctor$int32_5B_5D$int32"(ptr %managed.final.ptr, %absolute.array.int32.1 %items.value, i32 %_count.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i64 0

error.continue:                                   ; preds = %managed.end
  ret i64 %managed.handle
}

define %absolute.array.int32.1 @"std.collections.Vector<int32>.toArray"(ptr %this) {
entry:
  %i = alloca i32, align 4
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
  %array.data4 = insertvalue %absolute.array.int32.1 undef, ptr %array.data1, 0
  %array.owner5 = insertvalue %absolute.array.int32.1 %array.data4, ptr %array.owner2, 1
  %array.dimension6 = insertvalue %absolute.array.int32.1 %array.owner5, i64 %array.dimension3, 2
  store i32 0, ptr %i, align 4
  br label %while.condition

while.condition:                                  ; preds = %array.bounds.success21, %entry
  %i.value = load i32, ptr %i, align 4
  %_count.address7 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 2
  %_count.value8 = load i32, ptr %_count.address7, align 4
  %less = icmp slt i32 %i.value, %_count.value8
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %i.value9 = load i32, ptr %i, align 4
  %array.index.wide = sext i32 %i.value9 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension3
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %array.data25 = insertvalue %absolute.array.int32.1 undef, ptr %array.data1, 0
  %array.owner26 = insertvalue %absolute.array.int32.1 %array.data25, ptr %array.owner2, 1
  %array.dimension27 = insertvalue %absolute.array.int32.1 %array.owner26, i64 %array.dimension3, 2
  ret %absolute.array.int32.1 %array.dimension27

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data1, i32 %i.value9
  %items.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value = load %absolute.array.int32.1, ptr %items.address, align 8
  %array.data10 = extractvalue %absolute.array.int32.1 %items.value, 0
  %array.owner11 = extractvalue %absolute.array.int32.1 %items.value, 1
  %array.dimension12 = extractvalue %absolute.array.int32.1 %items.value, 2
  %items.address13 = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 1
  %items.value14 = load %absolute.array.int32.1, ptr %items.address13, align 8
  %array.data15 = extractvalue %absolute.array.int32.1 %items.value14, 0
  %array.owner16 = extractvalue %absolute.array.int32.1 %items.value14, 1
  %array.dimension17 = extractvalue %absolute.array.int32.1 %items.value14, 2
  %i.value18 = load i32, ptr %i, align 4
  %array.index.wide19 = sext i32 %i.value18 to i64
  %array.index.valid20 = icmp ult i64 %array.index.wide19, %array.dimension17
  br i1 %array.index.valid20, label %array.bounds.success21, label %array.bounds.failure22, !prof !0

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.34)
  call void @exit(i32 1)
  unreachable

array.bounds.success21:                           ; preds = %array.bounds.success
  %array.element.address23 = getelementptr inbounds i32, ptr %array.data15, i32 %i.value18
  %array.element = load i32, ptr %array.element.address23, align 4
  store i32 %array.element, ptr %array.element.address, align 4
  %i.value24 = load i32, ptr %i, align 4
  %add = add i32 %i.value24, 1
  store i32 %add, ptr %i, align 4
  br label %while.condition

array.bounds.failure22:                           ; preds = %array.bounds.success
  %1 = call i32 @puts(ptr @array.bounds.message.35)
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
  %_isShared.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %this, i32 0, i32 4
  store i1 false, ptr %_isShared.address, align 1
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
  %shifted = alloca i1, align 1
  %j = alloca i32, align 4
  %key = alloca i32, align 4
  %outer = alloca i32, align 4
  %i = alloca i32, align 4
  %state = alloca i32, align 4
  %values.cached.pointee = alloca ptr, align 8
  %values = alloca i64, align 8
  %SIZE = alloca i32, align 4
  store i32 100000, ptr %SIZE, align 4
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.Vector<int32>", ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 -6152174195361087120)
  %managed.id = trunc i64 %managed.handle to i32
  %0 = lshr i64 %managed.handle, 32
  %managed.gen = trunc i64 %0 to i32
  %managed.slots.size = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds = icmp ult i32 %managed.id, %managed.slots.size
  br i1 %managed.in.bounds, label %managed.fast.load, label %managed.slow

managed.fast.check:                               ; preds = %managed.fast.load
  %managed.slot.ptr.field = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 0
  %managed.ptr = load ptr, ptr %managed.slot.ptr.field, align 8
  %managed.ptr.not.null = icmp ne ptr %managed.ptr, null
  br i1 %managed.ptr.not.null, label %managed.fast.valid, label %managed.slow

managed.fast.load:                                ; preds = %entry
  %managed.slots.data = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr = getelementptr %absolute.Slot, ptr %managed.slots.data, i32 %managed.id
  %managed.slot.gen.ptr = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr, i32 0, i32 1
  %managed.slot.gen = load i32, ptr %managed.slot.gen.ptr, align 4
  %managed.gen.match = icmp eq i32 %managed.slot.gen, %managed.gen
  br i1 %managed.gen.match, label %managed.fast.check, label %managed.slow

managed.fast.valid:                               ; preds = %managed.fast.check
  br label %managed.end

managed.slow:                                     ; preds = %managed.fast.check, %managed.fast.load, %entry
  %managed.slow.ptr = call ptr @absolute_managed_require(i64 %managed.handle)
  br label %managed.end

managed.end:                                      ; preds = %managed.slow, %managed.fast.valid
  %managed.final.ptr = phi ptr [ %managed.ptr, %managed.fast.valid ], [ %managed.slow.ptr, %managed.slow ]
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.Vector<int32>", ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %"absolute.class.std.collections.Vector<int32>", ptr %managed.final.ptr, i32 0, i32 0
  store ptr @"std.collections.Vector<int32>.__vtable", ptr %vtable.address, align 8
  call void @"std.collections.Vector<int32>.__ctor"(ptr %managed.final.ptr)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  %cleanup.handle = load i64, ptr %values, align 4
  %managed.id6 = trunc i64 %cleanup.handle to i32
  %1 = lshr i64 %cleanup.handle, 32
  %managed.gen7 = trunc i64 %1 to i32
  %managed.slots.size8 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds9 = icmp ult i32 %managed.id6, %managed.slots.size8
  br i1 %managed.in.bounds9, label %managed.fast.load2, label %managed.slow4

error.continue:                                   ; preds = %managed.end
  store i64 %managed.handle, ptr %values, align 4
  store ptr %managed.final.ptr, ptr %values.cached.pointee, align 8
  store i32 123456789, ptr %state, align 4
  store i32 0, ptr %i, align 4
  br label %while.condition

managed.fast.check1:                              ; preds = %managed.fast.load2
  %managed.slot.ptr.field15 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr11, i32 0, i32 0
  %managed.ptr16 = load ptr, ptr %managed.slot.ptr.field15, align 8
  %managed.ptr.not.null17 = icmp ne ptr %managed.ptr16, null
  br i1 %managed.ptr.not.null17, label %managed.fast.valid3, label %managed.slow4

managed.fast.load2:                               ; preds = %error.propagate
  %managed.slots.data10 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr11 = getelementptr %absolute.Slot, ptr %managed.slots.data10, i32 %managed.id6
  %managed.slot.gen.ptr12 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr11, i32 0, i32 1
  %managed.slot.gen13 = load i32, ptr %managed.slot.gen.ptr12, align 4
  %managed.gen.match14 = icmp eq i32 %managed.slot.gen13, %managed.gen7
  br i1 %managed.gen.match14, label %managed.fast.check1, label %managed.slow4

managed.fast.valid3:                              ; preds = %managed.fast.check1
  br label %managed.end5

managed.slow4:                                    ; preds = %managed.fast.check1, %managed.fast.load2, %error.propagate
  %managed.slow.ptr18 = call ptr @absolute_managed_get(i64 %cleanup.handle)
  br label %managed.end5

managed.end5:                                     ; preds = %managed.slow4, %managed.fast.valid3
  %managed.final.ptr19 = phi ptr [ %managed.ptr16, %managed.fast.valid3 ], [ %managed.slow.ptr18, %managed.slow4 ]
  %aggregate.present = icmp ne ptr %managed.final.ptr19, null
  br i1 %aggregate.present, label %aggregate.cleanup, label %aggregate.cleanup.end

aggregate.cleanup:                                ; preds = %managed.end5
  %aggregate.vtable = load ptr, ptr %managed.final.ptr19, align 8
  %aggregate.destroy.slot = getelementptr ptr, ptr %aggregate.vtable, i64 0
  %aggregate.destroy = load ptr, ptr %aggregate.destroy.slot, align 8
  call void %aggregate.destroy(ptr %managed.final.ptr19)
  br label %aggregate.cleanup.end

aggregate.cleanup.end:                            ; preds = %aggregate.cleanup, %managed.end5
  call void @absolute_managed_destroy(i64 %cleanup.handle)
  store i64 0, ptr %values, align 4
  call void @absolute_error_report()
  ret i32 1

while.condition:                                  ; preds = %error.continue24, %error.continue
  %i.value = load i32, ptr %i, align 4
  %SIZE.value = load i32, ptr %SIZE, align 4
  %less = icmp slt i32 %i.value, %SIZE.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %state.value = load i32, ptr %state, align 4
  %mul = mul i32 %state.value, 1664525
  %add = add i32 %mul, 1013904223
  store i32 %add, ptr %state, align 4
  %values.value = load i64, ptr %values, align 4
  %values.cached.pointee20 = load ptr, ptr %values.cached.pointee, align 8
  %state.value21 = load i32, ptr %state, align 4
  call void @"std.collections.Vector<int32>.push$int32"(ptr %values.cached.pointee20, i32 %state.value21)
  %error.pending22 = call i1 @absolute_error_pending()
  br i1 %error.pending22, label %error.propagate23, label %error.continue24

while.end:                                        ; preds = %while.condition
  store i32 1, ptr %outer, align 4
  br label %while.condition52

error.propagate23:                                ; preds = %while.body
  %cleanup.handle25 = load i64, ptr %values, align 4
  %managed.id31 = trunc i64 %cleanup.handle25 to i32
  %2 = lshr i64 %cleanup.handle25, 32
  %managed.gen32 = trunc i64 %2 to i32
  %managed.slots.size33 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds34 = icmp ult i32 %managed.id31, %managed.slots.size33
  br i1 %managed.in.bounds34, label %managed.fast.load27, label %managed.slow29

error.continue24:                                 ; preds = %while.body
  %assignment.current = load i32, ptr %i, align 4
  %add51 = add i32 %assignment.current, 1
  store i32 %add51, ptr %i, align 4
  br label %while.condition

managed.fast.check26:                             ; preds = %managed.fast.load27
  %managed.slot.ptr.field40 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr36, i32 0, i32 0
  %managed.ptr41 = load ptr, ptr %managed.slot.ptr.field40, align 8
  %managed.ptr.not.null42 = icmp ne ptr %managed.ptr41, null
  br i1 %managed.ptr.not.null42, label %managed.fast.valid28, label %managed.slow29

managed.fast.load27:                              ; preds = %error.propagate23
  %managed.slots.data35 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr36 = getelementptr %absolute.Slot, ptr %managed.slots.data35, i32 %managed.id31
  %managed.slot.gen.ptr37 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr36, i32 0, i32 1
  %managed.slot.gen38 = load i32, ptr %managed.slot.gen.ptr37, align 4
  %managed.gen.match39 = icmp eq i32 %managed.slot.gen38, %managed.gen32
  br i1 %managed.gen.match39, label %managed.fast.check26, label %managed.slow29

managed.fast.valid28:                             ; preds = %managed.fast.check26
  br label %managed.end30

managed.slow29:                                   ; preds = %managed.fast.check26, %managed.fast.load27, %error.propagate23
  %managed.slow.ptr43 = call ptr @absolute_managed_get(i64 %cleanup.handle25)
  br label %managed.end30

managed.end30:                                    ; preds = %managed.slow29, %managed.fast.valid28
  %managed.final.ptr44 = phi ptr [ %managed.ptr41, %managed.fast.valid28 ], [ %managed.slow.ptr43, %managed.slow29 ]
  %aggregate.present47 = icmp ne ptr %managed.final.ptr44, null
  br i1 %aggregate.present47, label %aggregate.cleanup45, label %aggregate.cleanup.end46

aggregate.cleanup45:                              ; preds = %managed.end30
  %aggregate.vtable48 = load ptr, ptr %managed.final.ptr44, align 8
  %aggregate.destroy.slot49 = getelementptr ptr, ptr %aggregate.vtable48, i64 0
  %aggregate.destroy50 = load ptr, ptr %aggregate.destroy.slot49, align 8
  call void %aggregate.destroy50(ptr %managed.final.ptr44)
  br label %aggregate.cleanup.end46

aggregate.cleanup.end46:                          ; preds = %aggregate.cleanup45, %managed.end30
  call void @absolute_managed_destroy(i64 %cleanup.handle25)
  store i64 0, ptr %values, align 4
  store ptr null, ptr %values.cached.pointee, align 8
  call void @absolute_error_report()
  ret i32 1

while.condition52:                                ; preds = %error.continue173, %while.end
  %outer.value = load i32, ptr %outer, align 4
  %SIZE.value55 = load i32, ptr %SIZE, align 4
  %less56 = icmp slt i32 %outer.value, %SIZE.value55
  br i1 %less56, label %while.body53, label %while.end54

while.body53:                                     ; preds = %while.condition52
  %outer.value57 = load i32, ptr %outer, align 4
  %values.value58 = load i64, ptr %values, align 4
  %values.cached.pointee59 = load ptr, ptr %values.cached.pointee, align 8
  %property.result = call i32 @"std.collections.Vector<int32>.__absolute_indexer_get$int32"(ptr %values.cached.pointee59, i32 %outer.value57)
  %error.pending60 = call i1 @absolute_error_pending()
  br i1 %error.pending60, label %error.propagate61, label %error.continue62

while.end54:                                      ; preds = %while.condition52
  store i64 0, ptr %checksum, align 4
  store i32 0, ptr %i, align 4
  br label %while.condition202

error.propagate61:                                ; preds = %while.body53
  %cleanup.handle63 = load i64, ptr %values, align 4
  %managed.id69 = trunc i64 %cleanup.handle63 to i32
  %3 = lshr i64 %cleanup.handle63, 32
  %managed.gen70 = trunc i64 %3 to i32
  %managed.slots.size71 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds72 = icmp ult i32 %managed.id69, %managed.slots.size71
  br i1 %managed.in.bounds72, label %managed.fast.load65, label %managed.slow67

error.continue62:                                 ; preds = %while.body53
  store i32 %property.result, ptr %key, align 4
  %outer.value89 = load i32, ptr %outer, align 4
  %sub = sub i32 %outer.value89, 1
  store i32 %sub, ptr %j, align 4
  store i1 true, ptr %shifted, align 1
  br label %while.condition90

managed.fast.check64:                             ; preds = %managed.fast.load65
  %managed.slot.ptr.field78 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr74, i32 0, i32 0
  %managed.ptr79 = load ptr, ptr %managed.slot.ptr.field78, align 8
  %managed.ptr.not.null80 = icmp ne ptr %managed.ptr79, null
  br i1 %managed.ptr.not.null80, label %managed.fast.valid66, label %managed.slow67

managed.fast.load65:                              ; preds = %error.propagate61
  %managed.slots.data73 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr74 = getelementptr %absolute.Slot, ptr %managed.slots.data73, i32 %managed.id69
  %managed.slot.gen.ptr75 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr74, i32 0, i32 1
  %managed.slot.gen76 = load i32, ptr %managed.slot.gen.ptr75, align 4
  %managed.gen.match77 = icmp eq i32 %managed.slot.gen76, %managed.gen70
  br i1 %managed.gen.match77, label %managed.fast.check64, label %managed.slow67

managed.fast.valid66:                             ; preds = %managed.fast.check64
  br label %managed.end68

managed.slow67:                                   ; preds = %managed.fast.check64, %managed.fast.load65, %error.propagate61
  %managed.slow.ptr81 = call ptr @absolute_managed_get(i64 %cleanup.handle63)
  br label %managed.end68

managed.end68:                                    ; preds = %managed.slow67, %managed.fast.valid66
  %managed.final.ptr82 = phi ptr [ %managed.ptr79, %managed.fast.valid66 ], [ %managed.slow.ptr81, %managed.slow67 ]
  %aggregate.present85 = icmp ne ptr %managed.final.ptr82, null
  br i1 %aggregate.present85, label %aggregate.cleanup83, label %aggregate.cleanup.end84

aggregate.cleanup83:                              ; preds = %managed.end68
  %aggregate.vtable86 = load ptr, ptr %managed.final.ptr82, align 8
  %aggregate.destroy.slot87 = getelementptr ptr, ptr %aggregate.vtable86, i64 0
  %aggregate.destroy88 = load ptr, ptr %aggregate.destroy.slot87, align 8
  call void %aggregate.destroy88(ptr %managed.final.ptr82)
  br label %aggregate.cleanup.end84

aggregate.cleanup.end84:                          ; preds = %aggregate.cleanup83, %managed.end68
  call void @absolute_managed_destroy(i64 %cleanup.handle63)
  store i64 0, ptr %values, align 4
  store ptr null, ptr %values.cached.pointee, align 8
  call void @absolute_error_report()
  ret i32 1

while.condition90:                                ; preds = %if.end, %error.continue62
  %shifted.value = load i1, ptr %shifted, align 1
  br i1 %shifted.value, label %while.body91, label %while.end92

while.body91:                                     ; preds = %while.condition90
  %j.value = load i32, ptr %j, align 4
  %less93 = icmp slt i32 %j.value, 0
  br i1 %less93, label %if.body, label %if.next

while.end92:                                      ; preds = %while.condition90
  %j.value166 = load i32, ptr %j, align 4
  %add167 = add i32 %j.value166, 1
  %key.value168 = load i32, ptr %key, align 4
  %values.value169 = load i64, ptr %values, align 4
  %values.cached.pointee170 = load ptr, ptr %values.cached.pointee, align 8
  call void @"std.collections.Vector<int32>.__absolute_indexer_set$int32$int32"(ptr %values.cached.pointee170, i32 %add167, i32 %key.value168)
  %error.pending171 = call i1 @absolute_error_pending()
  br i1 %error.pending171, label %error.propagate172, label %error.continue173

if.end:                                           ; preds = %if.end127, %if.body
  br label %while.condition90

if.body:                                          ; preds = %while.body91
  store i1 false, ptr %shifted, align 1
  br label %if.end

if.next:                                          ; preds = %while.body91
  %j.value94 = load i32, ptr %j, align 4
  %values.value95 = load i64, ptr %values, align 4
  %values.cached.pointee96 = load ptr, ptr %values.cached.pointee, align 8
  %property.result97 = call i32 @"std.collections.Vector<int32>.__absolute_indexer_get$int32"(ptr %values.cached.pointee96, i32 %j.value94)
  %error.pending98 = call i1 @absolute_error_pending()
  br i1 %error.pending98, label %error.propagate99, label %error.continue100

error.propagate99:                                ; preds = %if.next
  %cleanup.handle101 = load i64, ptr %values, align 4
  %managed.id107 = trunc i64 %cleanup.handle101 to i32
  %4 = lshr i64 %cleanup.handle101, 32
  %managed.gen108 = trunc i64 %4 to i32
  %managed.slots.size109 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds110 = icmp ult i32 %managed.id107, %managed.slots.size109
  br i1 %managed.in.bounds110, label %managed.fast.load103, label %managed.slow105

error.continue100:                                ; preds = %if.next
  store i32 %property.result97, ptr %cur, align 4
  %cur.value = load i32, ptr %cur, align 4
  %key.value = load i32, ptr %key, align 4
  %greater = icmp sgt i32 %cur.value, %key.value
  br i1 %greater, label %if.body128, label %if.next129

managed.fast.check102:                            ; preds = %managed.fast.load103
  %managed.slot.ptr.field116 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr112, i32 0, i32 0
  %managed.ptr117 = load ptr, ptr %managed.slot.ptr.field116, align 8
  %managed.ptr.not.null118 = icmp ne ptr %managed.ptr117, null
  br i1 %managed.ptr.not.null118, label %managed.fast.valid104, label %managed.slow105

managed.fast.load103:                             ; preds = %error.propagate99
  %managed.slots.data111 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr112 = getelementptr %absolute.Slot, ptr %managed.slots.data111, i32 %managed.id107
  %managed.slot.gen.ptr113 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr112, i32 0, i32 1
  %managed.slot.gen114 = load i32, ptr %managed.slot.gen.ptr113, align 4
  %managed.gen.match115 = icmp eq i32 %managed.slot.gen114, %managed.gen108
  br i1 %managed.gen.match115, label %managed.fast.check102, label %managed.slow105

managed.fast.valid104:                            ; preds = %managed.fast.check102
  br label %managed.end106

managed.slow105:                                  ; preds = %managed.fast.check102, %managed.fast.load103, %error.propagate99
  %managed.slow.ptr119 = call ptr @absolute_managed_get(i64 %cleanup.handle101)
  br label %managed.end106

managed.end106:                                   ; preds = %managed.slow105, %managed.fast.valid104
  %managed.final.ptr120 = phi ptr [ %managed.ptr117, %managed.fast.valid104 ], [ %managed.slow.ptr119, %managed.slow105 ]
  %aggregate.present123 = icmp ne ptr %managed.final.ptr120, null
  br i1 %aggregate.present123, label %aggregate.cleanup121, label %aggregate.cleanup.end122

aggregate.cleanup121:                             ; preds = %managed.end106
  %aggregate.vtable124 = load ptr, ptr %managed.final.ptr120, align 8
  %aggregate.destroy.slot125 = getelementptr ptr, ptr %aggregate.vtable124, i64 0
  %aggregate.destroy126 = load ptr, ptr %aggregate.destroy.slot125, align 8
  call void %aggregate.destroy126(ptr %managed.final.ptr120)
  br label %aggregate.cleanup.end122

aggregate.cleanup.end122:                         ; preds = %aggregate.cleanup121, %managed.end106
  call void @absolute_managed_destroy(i64 %cleanup.handle101)
  store i64 0, ptr %values, align 4
  store ptr null, ptr %values.cached.pointee, align 8
  call void @absolute_error_report()
  ret i32 1

if.end127:                                        ; preds = %if.next129, %error.continue137
  br label %if.end

if.body128:                                       ; preds = %error.continue100
  %j.value130 = load i32, ptr %j, align 4
  %add131 = add i32 %j.value130, 1
  %cur.value132 = load i32, ptr %cur, align 4
  %values.value133 = load i64, ptr %values, align 4
  %values.cached.pointee134 = load ptr, ptr %values.cached.pointee, align 8
  call void @"std.collections.Vector<int32>.__absolute_indexer_set$int32$int32"(ptr %values.cached.pointee134, i32 %add131, i32 %cur.value132)
  %error.pending135 = call i1 @absolute_error_pending()
  br i1 %error.pending135, label %error.propagate136, label %error.continue137

if.next129:                                       ; preds = %error.continue100
  store i1 false, ptr %shifted, align 1
  br label %if.end127

error.propagate136:                               ; preds = %if.body128
  %cleanup.handle138 = load i64, ptr %values, align 4
  %managed.id144 = trunc i64 %cleanup.handle138 to i32
  %5 = lshr i64 %cleanup.handle138, 32
  %managed.gen145 = trunc i64 %5 to i32
  %managed.slots.size146 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds147 = icmp ult i32 %managed.id144, %managed.slots.size146
  br i1 %managed.in.bounds147, label %managed.fast.load140, label %managed.slow142

error.continue137:                                ; preds = %if.body128
  %assignment.current164 = load i32, ptr %j, align 4
  %sub165 = sub i32 %assignment.current164, 1
  store i32 %sub165, ptr %j, align 4
  br label %if.end127

managed.fast.check139:                            ; preds = %managed.fast.load140
  %managed.slot.ptr.field153 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr149, i32 0, i32 0
  %managed.ptr154 = load ptr, ptr %managed.slot.ptr.field153, align 8
  %managed.ptr.not.null155 = icmp ne ptr %managed.ptr154, null
  br i1 %managed.ptr.not.null155, label %managed.fast.valid141, label %managed.slow142

managed.fast.load140:                             ; preds = %error.propagate136
  %managed.slots.data148 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr149 = getelementptr %absolute.Slot, ptr %managed.slots.data148, i32 %managed.id144
  %managed.slot.gen.ptr150 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr149, i32 0, i32 1
  %managed.slot.gen151 = load i32, ptr %managed.slot.gen.ptr150, align 4
  %managed.gen.match152 = icmp eq i32 %managed.slot.gen151, %managed.gen145
  br i1 %managed.gen.match152, label %managed.fast.check139, label %managed.slow142

managed.fast.valid141:                            ; preds = %managed.fast.check139
  br label %managed.end143

managed.slow142:                                  ; preds = %managed.fast.check139, %managed.fast.load140, %error.propagate136
  %managed.slow.ptr156 = call ptr @absolute_managed_get(i64 %cleanup.handle138)
  br label %managed.end143

managed.end143:                                   ; preds = %managed.slow142, %managed.fast.valid141
  %managed.final.ptr157 = phi ptr [ %managed.ptr154, %managed.fast.valid141 ], [ %managed.slow.ptr156, %managed.slow142 ]
  %aggregate.present160 = icmp ne ptr %managed.final.ptr157, null
  br i1 %aggregate.present160, label %aggregate.cleanup158, label %aggregate.cleanup.end159

aggregate.cleanup158:                             ; preds = %managed.end143
  %aggregate.vtable161 = load ptr, ptr %managed.final.ptr157, align 8
  %aggregate.destroy.slot162 = getelementptr ptr, ptr %aggregate.vtable161, i64 0
  %aggregate.destroy163 = load ptr, ptr %aggregate.destroy.slot162, align 8
  call void %aggregate.destroy163(ptr %managed.final.ptr157)
  br label %aggregate.cleanup.end159

aggregate.cleanup.end159:                         ; preds = %aggregate.cleanup158, %managed.end143
  call void @absolute_managed_destroy(i64 %cleanup.handle138)
  store i64 0, ptr %values, align 4
  store ptr null, ptr %values.cached.pointee, align 8
  call void @absolute_error_report()
  ret i32 1

error.propagate172:                               ; preds = %while.end92
  %cleanup.handle174 = load i64, ptr %values, align 4
  %managed.id180 = trunc i64 %cleanup.handle174 to i32
  %6 = lshr i64 %cleanup.handle174, 32
  %managed.gen181 = trunc i64 %6 to i32
  %managed.slots.size182 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds183 = icmp ult i32 %managed.id180, %managed.slots.size182
  br i1 %managed.in.bounds183, label %managed.fast.load176, label %managed.slow178

error.continue173:                                ; preds = %while.end92
  %assignment.current200 = load i32, ptr %outer, align 4
  %add201 = add i32 %assignment.current200, 1
  store i32 %add201, ptr %outer, align 4
  br label %while.condition52, !llvm.loop !1

managed.fast.check175:                            ; preds = %managed.fast.load176
  %managed.slot.ptr.field189 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr185, i32 0, i32 0
  %managed.ptr190 = load ptr, ptr %managed.slot.ptr.field189, align 8
  %managed.ptr.not.null191 = icmp ne ptr %managed.ptr190, null
  br i1 %managed.ptr.not.null191, label %managed.fast.valid177, label %managed.slow178

managed.fast.load176:                             ; preds = %error.propagate172
  %managed.slots.data184 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr185 = getelementptr %absolute.Slot, ptr %managed.slots.data184, i32 %managed.id180
  %managed.slot.gen.ptr186 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr185, i32 0, i32 1
  %managed.slot.gen187 = load i32, ptr %managed.slot.gen.ptr186, align 4
  %managed.gen.match188 = icmp eq i32 %managed.slot.gen187, %managed.gen181
  br i1 %managed.gen.match188, label %managed.fast.check175, label %managed.slow178

managed.fast.valid177:                            ; preds = %managed.fast.check175
  br label %managed.end179

managed.slow178:                                  ; preds = %managed.fast.check175, %managed.fast.load176, %error.propagate172
  %managed.slow.ptr192 = call ptr @absolute_managed_get(i64 %cleanup.handle174)
  br label %managed.end179

managed.end179:                                   ; preds = %managed.slow178, %managed.fast.valid177
  %managed.final.ptr193 = phi ptr [ %managed.ptr190, %managed.fast.valid177 ], [ %managed.slow.ptr192, %managed.slow178 ]
  %aggregate.present196 = icmp ne ptr %managed.final.ptr193, null
  br i1 %aggregate.present196, label %aggregate.cleanup194, label %aggregate.cleanup.end195

aggregate.cleanup194:                             ; preds = %managed.end179
  %aggregate.vtable197 = load ptr, ptr %managed.final.ptr193, align 8
  %aggregate.destroy.slot198 = getelementptr ptr, ptr %aggregate.vtable197, i64 0
  %aggregate.destroy199 = load ptr, ptr %aggregate.destroy.slot198, align 8
  call void %aggregate.destroy199(ptr %managed.final.ptr193)
  br label %aggregate.cleanup.end195

aggregate.cleanup.end195:                         ; preds = %aggregate.cleanup194, %managed.end179
  call void @absolute_managed_destroy(i64 %cleanup.handle174)
  store i64 0, ptr %values, align 4
  store ptr null, ptr %values.cached.pointee, align 8
  call void @absolute_error_report()
  ret i32 1

while.condition202:                               ; preds = %error.continue214, %while.end54
  %i.value205 = load i32, ptr %i, align 4
  %SIZE.value206 = load i32, ptr %SIZE, align 4
  %less207 = icmp slt i32 %i.value205, %SIZE.value206
  br i1 %less207, label %while.body203, label %while.end204

while.body203:                                    ; preds = %while.condition202
  %i.value208 = load i32, ptr %i, align 4
  %values.value209 = load i64, ptr %values, align 4
  %values.cached.pointee210 = load ptr, ptr %values.cached.pointee, align 8
  %property.result211 = call i32 @"std.collections.Vector<int32>.__absolute_indexer_get$int32"(ptr %values.cached.pointee210, i32 %i.value208)
  %error.pending212 = call i1 @absolute_error_pending()
  br i1 %error.pending212, label %error.propagate213, label %error.continue214

while.end204:                                     ; preds = %while.condition202
  %values.value245 = load i64, ptr %values, align 4
  %values.cached.pointee246 = load ptr, ptr %values.cached.pointee, align 8
  %property.result247 = call i32 @"std.collections.Vector<int32>.__absolute_indexer_get$int32"(ptr %values.cached.pointee246, i32 0)
  %error.pending248 = call i1 @absolute_error_pending()
  br i1 %error.pending248, label %error.propagate249, label %error.continue250

error.propagate213:                               ; preds = %while.body203
  %cleanup.handle215 = load i64, ptr %values, align 4
  %managed.id221 = trunc i64 %cleanup.handle215 to i32
  %7 = lshr i64 %cleanup.handle215, 32
  %managed.gen222 = trunc i64 %7 to i32
  %managed.slots.size223 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds224 = icmp ult i32 %managed.id221, %managed.slots.size223
  br i1 %managed.in.bounds224, label %managed.fast.load217, label %managed.slow219

error.continue214:                                ; preds = %while.body203
  %assignment.current241 = load i64, ptr %checksum, align 4
  %int.cast = sext i32 %property.result211 to i64
  %add242 = add i64 %assignment.current241, %int.cast
  store i64 %add242, ptr %checksum, align 4
  %assignment.current243 = load i32, ptr %i, align 4
  %add244 = add i32 %assignment.current243, 1
  store i32 %add244, ptr %i, align 4
  br label %while.condition202

managed.fast.check216:                            ; preds = %managed.fast.load217
  %managed.slot.ptr.field230 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr226, i32 0, i32 0
  %managed.ptr231 = load ptr, ptr %managed.slot.ptr.field230, align 8
  %managed.ptr.not.null232 = icmp ne ptr %managed.ptr231, null
  br i1 %managed.ptr.not.null232, label %managed.fast.valid218, label %managed.slow219

managed.fast.load217:                             ; preds = %error.propagate213
  %managed.slots.data225 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr226 = getelementptr %absolute.Slot, ptr %managed.slots.data225, i32 %managed.id221
  %managed.slot.gen.ptr227 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr226, i32 0, i32 1
  %managed.slot.gen228 = load i32, ptr %managed.slot.gen.ptr227, align 4
  %managed.gen.match229 = icmp eq i32 %managed.slot.gen228, %managed.gen222
  br i1 %managed.gen.match229, label %managed.fast.check216, label %managed.slow219

managed.fast.valid218:                            ; preds = %managed.fast.check216
  br label %managed.end220

managed.slow219:                                  ; preds = %managed.fast.check216, %managed.fast.load217, %error.propagate213
  %managed.slow.ptr233 = call ptr @absolute_managed_get(i64 %cleanup.handle215)
  br label %managed.end220

managed.end220:                                   ; preds = %managed.slow219, %managed.fast.valid218
  %managed.final.ptr234 = phi ptr [ %managed.ptr231, %managed.fast.valid218 ], [ %managed.slow.ptr233, %managed.slow219 ]
  %aggregate.present237 = icmp ne ptr %managed.final.ptr234, null
  br i1 %aggregate.present237, label %aggregate.cleanup235, label %aggregate.cleanup.end236

aggregate.cleanup235:                             ; preds = %managed.end220
  %aggregate.vtable238 = load ptr, ptr %managed.final.ptr234, align 8
  %aggregate.destroy.slot239 = getelementptr ptr, ptr %aggregate.vtable238, i64 0
  %aggregate.destroy240 = load ptr, ptr %aggregate.destroy.slot239, align 8
  call void %aggregate.destroy240(ptr %managed.final.ptr234)
  br label %aggregate.cleanup.end236

aggregate.cleanup.end236:                         ; preds = %aggregate.cleanup235, %managed.end220
  call void @absolute_managed_destroy(i64 %cleanup.handle215)
  store i64 0, ptr %values, align 4
  store ptr null, ptr %values.cached.pointee, align 8
  call void @absolute_error_report()
  ret i32 1

error.propagate249:                               ; preds = %while.end204
  %cleanup.handle251 = load i64, ptr %values, align 4
  %managed.id257 = trunc i64 %cleanup.handle251 to i32
  %8 = lshr i64 %cleanup.handle251, 32
  %managed.gen258 = trunc i64 %8 to i32
  %managed.slots.size259 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds260 = icmp ult i32 %managed.id257, %managed.slots.size259
  br i1 %managed.in.bounds260, label %managed.fast.load253, label %managed.slow255

error.continue250:                                ; preds = %while.end204
  store i32 %property.result247, ptr %first, align 4
  %SIZE.value277 = load i32, ptr %SIZE, align 4
  %sub278 = sub i32 %SIZE.value277, 1
  %values.value279 = load i64, ptr %values, align 4
  %values.cached.pointee280 = load ptr, ptr %values.cached.pointee, align 8
  %property.result281 = call i32 @"std.collections.Vector<int32>.__absolute_indexer_get$int32"(ptr %values.cached.pointee280, i32 %sub278)
  %error.pending282 = call i1 @absolute_error_pending()
  br i1 %error.pending282, label %error.propagate283, label %error.continue284

managed.fast.check252:                            ; preds = %managed.fast.load253
  %managed.slot.ptr.field266 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr262, i32 0, i32 0
  %managed.ptr267 = load ptr, ptr %managed.slot.ptr.field266, align 8
  %managed.ptr.not.null268 = icmp ne ptr %managed.ptr267, null
  br i1 %managed.ptr.not.null268, label %managed.fast.valid254, label %managed.slow255

managed.fast.load253:                             ; preds = %error.propagate249
  %managed.slots.data261 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr262 = getelementptr %absolute.Slot, ptr %managed.slots.data261, i32 %managed.id257
  %managed.slot.gen.ptr263 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr262, i32 0, i32 1
  %managed.slot.gen264 = load i32, ptr %managed.slot.gen.ptr263, align 4
  %managed.gen.match265 = icmp eq i32 %managed.slot.gen264, %managed.gen258
  br i1 %managed.gen.match265, label %managed.fast.check252, label %managed.slow255

managed.fast.valid254:                            ; preds = %managed.fast.check252
  br label %managed.end256

managed.slow255:                                  ; preds = %managed.fast.check252, %managed.fast.load253, %error.propagate249
  %managed.slow.ptr269 = call ptr @absolute_managed_get(i64 %cleanup.handle251)
  br label %managed.end256

managed.end256:                                   ; preds = %managed.slow255, %managed.fast.valid254
  %managed.final.ptr270 = phi ptr [ %managed.ptr267, %managed.fast.valid254 ], [ %managed.slow.ptr269, %managed.slow255 ]
  %aggregate.present273 = icmp ne ptr %managed.final.ptr270, null
  br i1 %aggregate.present273, label %aggregate.cleanup271, label %aggregate.cleanup.end272

aggregate.cleanup271:                             ; preds = %managed.end256
  %aggregate.vtable274 = load ptr, ptr %managed.final.ptr270, align 8
  %aggregate.destroy.slot275 = getelementptr ptr, ptr %aggregate.vtable274, i64 0
  %aggregate.destroy276 = load ptr, ptr %aggregate.destroy.slot275, align 8
  call void %aggregate.destroy276(ptr %managed.final.ptr270)
  br label %aggregate.cleanup.end272

aggregate.cleanup.end272:                         ; preds = %aggregate.cleanup271, %managed.end256
  call void @absolute_managed_destroy(i64 %cleanup.handle251)
  store i64 0, ptr %values, align 4
  store ptr null, ptr %values.cached.pointee, align 8
  call void @absolute_error_report()
  ret i32 1

error.propagate283:                               ; preds = %error.continue250
  %cleanup.handle285 = load i64, ptr %values, align 4
  %managed.id291 = trunc i64 %cleanup.handle285 to i32
  %9 = lshr i64 %cleanup.handle285, 32
  %managed.gen292 = trunc i64 %9 to i32
  %managed.slots.size293 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds294 = icmp ult i32 %managed.id291, %managed.slots.size293
  br i1 %managed.in.bounds294, label %managed.fast.load287, label %managed.slow289

error.continue284:                                ; preds = %error.continue250
  store i32 %property.result281, ptr %last, align 4
  %first.value = load i32, ptr %first, align 4
  %int.cast311 = sext i32 %first.value to i64
  %mul312 = mul i64 %int.cast311, 31
  %assignment.current313 = load i64, ptr %checksum, align 4
  %add314 = add i64 %assignment.current313, %mul312
  store i64 %add314, ptr %checksum, align 4
  %last.value = load i32, ptr %last, align 4
  %int.cast315 = sext i32 %last.value to i64
  %mul316 = mul i64 %int.cast315, 17
  %assignment.current317 = load i64, ptr %checksum, align 4
  %add318 = add i64 %assignment.current317, %mul316
  store i64 %add318, ptr %checksum, align 4
  %checksum.value = load i64, ptr %checksum, align 4
  %print.result = call i32 (ptr, ...) @printf(ptr @print.format, i64 %checksum.value)
  %cleanup.handle319 = load i64, ptr %values, align 4
  %managed.id325 = trunc i64 %cleanup.handle319 to i32
  %10 = lshr i64 %cleanup.handle319, 32
  %managed.gen326 = trunc i64 %10 to i32
  %managed.slots.size327 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds328 = icmp ult i32 %managed.id325, %managed.slots.size327
  br i1 %managed.in.bounds328, label %managed.fast.load321, label %managed.slow323

managed.fast.check286:                            ; preds = %managed.fast.load287
  %managed.slot.ptr.field300 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr296, i32 0, i32 0
  %managed.ptr301 = load ptr, ptr %managed.slot.ptr.field300, align 8
  %managed.ptr.not.null302 = icmp ne ptr %managed.ptr301, null
  br i1 %managed.ptr.not.null302, label %managed.fast.valid288, label %managed.slow289

managed.fast.load287:                             ; preds = %error.propagate283
  %managed.slots.data295 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr296 = getelementptr %absolute.Slot, ptr %managed.slots.data295, i32 %managed.id291
  %managed.slot.gen.ptr297 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr296, i32 0, i32 1
  %managed.slot.gen298 = load i32, ptr %managed.slot.gen.ptr297, align 4
  %managed.gen.match299 = icmp eq i32 %managed.slot.gen298, %managed.gen292
  br i1 %managed.gen.match299, label %managed.fast.check286, label %managed.slow289

managed.fast.valid288:                            ; preds = %managed.fast.check286
  br label %managed.end290

managed.slow289:                                  ; preds = %managed.fast.check286, %managed.fast.load287, %error.propagate283
  %managed.slow.ptr303 = call ptr @absolute_managed_get(i64 %cleanup.handle285)
  br label %managed.end290

managed.end290:                                   ; preds = %managed.slow289, %managed.fast.valid288
  %managed.final.ptr304 = phi ptr [ %managed.ptr301, %managed.fast.valid288 ], [ %managed.slow.ptr303, %managed.slow289 ]
  %aggregate.present307 = icmp ne ptr %managed.final.ptr304, null
  br i1 %aggregate.present307, label %aggregate.cleanup305, label %aggregate.cleanup.end306

aggregate.cleanup305:                             ; preds = %managed.end290
  %aggregate.vtable308 = load ptr, ptr %managed.final.ptr304, align 8
  %aggregate.destroy.slot309 = getelementptr ptr, ptr %aggregate.vtable308, i64 0
  %aggregate.destroy310 = load ptr, ptr %aggregate.destroy.slot309, align 8
  call void %aggregate.destroy310(ptr %managed.final.ptr304)
  br label %aggregate.cleanup.end306

aggregate.cleanup.end306:                         ; preds = %aggregate.cleanup305, %managed.end290
  call void @absolute_managed_destroy(i64 %cleanup.handle285)
  store i64 0, ptr %values, align 4
  store ptr null, ptr %values.cached.pointee, align 8
  call void @absolute_error_report()
  ret i32 1

managed.fast.check320:                            ; preds = %managed.fast.load321
  %managed.slot.ptr.field334 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr330, i32 0, i32 0
  %managed.ptr335 = load ptr, ptr %managed.slot.ptr.field334, align 8
  %managed.ptr.not.null336 = icmp ne ptr %managed.ptr335, null
  br i1 %managed.ptr.not.null336, label %managed.fast.valid322, label %managed.slow323

managed.fast.load321:                             ; preds = %error.continue284
  %managed.slots.data329 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr330 = getelementptr %absolute.Slot, ptr %managed.slots.data329, i32 %managed.id325
  %managed.slot.gen.ptr331 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr330, i32 0, i32 1
  %managed.slot.gen332 = load i32, ptr %managed.slot.gen.ptr331, align 4
  %managed.gen.match333 = icmp eq i32 %managed.slot.gen332, %managed.gen326
  br i1 %managed.gen.match333, label %managed.fast.check320, label %managed.slow323

managed.fast.valid322:                            ; preds = %managed.fast.check320
  br label %managed.end324

managed.slow323:                                  ; preds = %managed.fast.check320, %managed.fast.load321, %error.continue284
  %managed.slow.ptr337 = call ptr @absolute_managed_get(i64 %cleanup.handle319)
  br label %managed.end324

managed.end324:                                   ; preds = %managed.slow323, %managed.fast.valid322
  %managed.final.ptr338 = phi ptr [ %managed.ptr335, %managed.fast.valid322 ], [ %managed.slow.ptr337, %managed.slow323 ]
  %aggregate.present341 = icmp ne ptr %managed.final.ptr338, null
  br i1 %aggregate.present341, label %aggregate.cleanup339, label %aggregate.cleanup.end340

aggregate.cleanup339:                             ; preds = %managed.end324
  %aggregate.vtable342 = load ptr, ptr %managed.final.ptr338, align 8
  %aggregate.destroy.slot343 = getelementptr ptr, ptr %aggregate.vtable342, i64 0
  %aggregate.destroy344 = load ptr, ptr %aggregate.destroy.slot343, align 8
  call void %aggregate.destroy344(ptr %managed.final.ptr338)
  br label %aggregate.cleanup.end340

aggregate.cleanup.end340:                         ; preds = %aggregate.cleanup339, %managed.end324
  call void @absolute_managed_destroy(i64 %cleanup.handle319)
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

attributes #0 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #1 = { cold noreturn }
attributes #2 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }

!0 = !{!"branch_weights", i32 2000, i32 1}
!1 = distinct !{!1, !2}
!2 = !{!"llvm.loop.unroll.count", i32 4}
