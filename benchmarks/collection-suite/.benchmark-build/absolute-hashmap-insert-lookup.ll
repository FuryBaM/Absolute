; ModuleID = 'hashmap-insert-lookup.abs'
source_filename = "hashmap-insert-lookup.abs"
target triple = "x86_64-pc-windows-msvc"

%absolute.closure = type { i64, ptr, ptr, ptr }
%"absolute.struct.std.collections.HashKeyValuePair<int32,int32>" = type { i32, i32 }
%absolute.class.Error = type { ptr, ptr }
%"absolute.class.std.collections.HashMapIterator<int32,int32>" = type { ptr, %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1", i32 }
%"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" = type { ptr, ptr, i64 }
%"absolute.class.std.collections.HashMap<int32,int32>" = type { ptr, %absolute.array.int32.1, %absolute.array.int32.1, %absolute.array.int32.1, i32, i32, i32, ptr, ptr }
%absolute.array.int32.1 = type { ptr, ptr, i64 }
%absolute.Slot = type { ptr, i32, i32, i64 }

@Error.__vtable = internal constant [1 x ptr] [ptr @Error.__destroy], align 8
@"std.collections.HashMapIterator<int32,int32>.__vtable" = internal constant [1 x ptr] [ptr @"std.collections.HashMapIterator<int32,int32>.__destroy"], align 8
@"std.collections.HashMap<int32,int32>.__vtable" = internal constant [1 x ptr] [ptr @"std.collections.HashMap<int32,int32>.__destroy"], align 8
@absolute_managed_slots_size = external global i32
@absolute_managed_slots_data = external global ptr
@__absolute.closure.value.hashInt32 = internal constant %absolute.closure { i64 -1, ptr @__absolute.closure.invoke.hashInt32, ptr null, ptr null }
@__absolute.closure.value.equalsInt32 = internal constant %absolute.closure { i64 -1, ptr @__absolute.closure.invoke.equalsInt32, ptr null, ptr null }
@print.format = private unnamed_addr constant [6 x i8] c"%lld\0A\00", align 1
@array.bounds.message = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.1 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.2 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.3 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.4 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@function.null.message = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@function.null.message.5 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.6 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.7 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.8 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.9 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.10 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.11 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.12 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.13 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.14 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.15 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.16 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.17 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.18 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.19 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.20 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.21 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.22 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.23 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal = private unnamed_addr constant [14 x i8] c"Key not found\00", align 1
@array.bounds.message.24 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.25 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.26 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.27 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.28 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.29 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.30 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@array.bounds.message.31 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1
@string.literal.32 = private unnamed_addr constant [34 x i8] c"HashMap capacity must be positive\00", align 1
@array.bounds.message.33 = private unnamed_addr constant [26 x i8] c"Array index out of bounds\00", align 1

define void @"std.collections.HashKeyValuePair<int32,int32>.initialize$int32$int32"(ptr %this, i32 %entryKey, i32 %entryValue) {
entry:
  %entryValue2 = alloca i32, align 4
  %entryKey1 = alloca i32, align 4
  store i32 %entryKey, ptr %entryKey1, align 4
  store i32 %entryValue, ptr %entryValue2, align 4
  %key.address = getelementptr inbounds %"absolute.struct.std.collections.HashKeyValuePair<int32,int32>", ptr %this, i32 0, i32 0
  %entryKey.value = load i32, ptr %entryKey1, align 4
  store i32 %entryKey.value, ptr %key.address, align 4
  %value.address = getelementptr inbounds %"absolute.struct.std.collections.HashKeyValuePair<int32,int32>", ptr %this, i32 0, i32 1
  %entryValue.value = load i32, ptr %entryValue2, align 4
  store i32 %entryValue.value, ptr %value.address, align 4
  ret void
}

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

define i1 @"std.collections.HashMapIterator<int32,int32>.next"(ptr %this) {
entry:
  %index.address = getelementptr inbounds %"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr %this, i32 0, i32 2
  %index.address1 = getelementptr inbounds %"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr %this, i32 0, i32 2
  %index.value = load i32, ptr %index.address1, align 4
  %add = add i32 %index.value, 1
  store i32 %add, ptr %index.address, align 4
  %index.address2 = getelementptr inbounds %"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr %this, i32 0, i32 2
  %index.value3 = load i32, ptr %index.address2, align 4
  %snapshot.address = getelementptr inbounds %"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr %this, i32 0, i32 1
  %snapshot.value = load %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1", ptr %snapshot.address, align 8
  %array.length.i64 = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %snapshot.value, 2
  %array.length = trunc i64 %array.length.i64 to i32
  %less = icmp slt i32 %index.value3, %array.length
  ret i1 %less
}

define %"absolute.struct.std.collections.HashKeyValuePair<int32,int32>" @"std.collections.HashMapIterator<int32,int32>.__absolute_property_get_value"(ptr %this) {
entry:
  %snapshot.address = getelementptr inbounds %"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr %this, i32 0, i32 1
  %snapshot.value = load %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1", ptr %snapshot.address, align 8
  %array.data = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %snapshot.value, 0
  %array.owner = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %snapshot.value, 1
  %array.dimension = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %snapshot.value, 2
  %snapshot.address1 = getelementptr inbounds %"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr %this, i32 0, i32 1
  %snapshot.value2 = load %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1", ptr %snapshot.address1, align 8
  %array.data3 = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %snapshot.value2, 0
  %array.owner4 = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %snapshot.value2, 1
  %array.dimension5 = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %snapshot.value2, 2
  %index.address = getelementptr inbounds %"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr %this, i32 0, i32 2
  %index.value = load i32, ptr %index.address, align 4
  %array.index.wide = sext i32 %index.value to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension5
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

array.bounds.success:                             ; preds = %entry
  %array.element.address = getelementptr inbounds %"absolute.struct.std.collections.HashKeyValuePair<int32,int32>", ptr %array.data3, i32 %index.value
  %array.element = load %"absolute.struct.std.collections.HashKeyValuePair<int32,int32>", ptr %array.element.address, align 4
  ret %"absolute.struct.std.collections.HashKeyValuePair<int32,int32>" %array.element

array.bounds.failure:                             ; preds = %entry
  %0 = call i32 @puts(ptr @array.bounds.message)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.HashMapIterator<int32,int32>.__ctor$std_2Ecollections_2EHashKeyValuePair_3Cint32_2Cint32_3E_5B_5D"(ptr %this, %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %entries) {
entry:
  %array.data = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %entries, 0
  %array.owner = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %entries, 1
  %array.dimension = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %entries, 2
  %snapshot.address = getelementptr inbounds %"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr %this, i32 0, i32 1
  %copy.element.count = mul i64 1, %array.dimension
  %copy.byte.count = mul i64 %copy.element.count, 8
  %copy.data = call ptr @malloc(i64 %copy.byte.count)
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %copy.data, ptr align 1 %array.data, i64 %copy.byte.count, i1 false)
  %array.data1 = insertvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" undef, ptr %copy.data, 0
  %array.owner2 = insertvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.data1, ptr %copy.data, 1
  %array.dimension3 = insertvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.owner2, i64 %array.dimension, 2
  %field.cleanup.array = load %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1", ptr %snapshot.address, align 8
  %field.cleanup.array.owner = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" zeroinitializer, ptr %snapshot.address, align 8
  store %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.dimension3, ptr %snapshot.address, align 8
  %index.address = getelementptr inbounds %"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr %this, i32 0, i32 2
  store i32 -1, ptr %index.address, align 4
  ret void
}

define internal void @"std.collections.HashMapIterator<int32,int32>.__destroy"(ptr %this) {
entry:
  %snapshot.address = getelementptr inbounds %"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr %this, i32 0, i32 1
  %field.cleanup.array = load %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1", ptr %snapshot.address, align 8
  %field.cleanup.array.owner = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" zeroinitializer, ptr %snapshot.address, align 8
  ret void
}

define void @"std.collections.HashMap<int32,int32>.insertRehashed$int32$int32"(ptr %this, i32 %key, i32 %value) {
entry:
  %index = alloca i32, align 4
  %value2 = alloca i32, align 4
  %key1 = alloca i32, align 4
  store i32 %key, ptr %key1, align 4
  store i32 %value, ptr %value2, align 4
  %key.value = load i32, ptr %key1, align 4
  %findInsertionIndex.result = call i32 @"std.collections.HashMap<int32,int32>.findInsertionIndex$int32"(ptr %this, i32 %key.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  ret void

error.continue:                                   ; preds = %entry
  store i32 %findInsertionIndex.result, ptr %index, align 4
  %keys.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %keys.value = load %absolute.array.int32.1, ptr %keys.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %keys.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %keys.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %keys.value, 2
  %keys.address3 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %keys.value4 = load %absolute.array.int32.1, ptr %keys.address3, align 8
  %array.data5 = extractvalue %absolute.array.int32.1 %keys.value4, 0
  %array.owner6 = extractvalue %absolute.array.int32.1 %keys.value4, 1
  %array.dimension7 = extractvalue %absolute.array.int32.1 %keys.value4, 2
  %index.value = load i32, ptr %index, align 4
  %array.index.wide = sext i32 %index.value to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension7
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

array.bounds.success:                             ; preds = %error.continue
  %array.element.address = getelementptr inbounds i32, ptr %array.data5, i32 %index.value
  %key.value8 = load i32, ptr %key1, align 4
  store i32 %key.value8, ptr %array.element.address, align 4
  %values.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value = load %absolute.array.int32.1, ptr %values.address, align 8
  %array.data9 = extractvalue %absolute.array.int32.1 %values.value, 0
  %array.owner10 = extractvalue %absolute.array.int32.1 %values.value, 1
  %array.dimension11 = extractvalue %absolute.array.int32.1 %values.value, 2
  %values.address12 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value13 = load %absolute.array.int32.1, ptr %values.address12, align 8
  %array.data14 = extractvalue %absolute.array.int32.1 %values.value13, 0
  %array.owner15 = extractvalue %absolute.array.int32.1 %values.value13, 1
  %array.dimension16 = extractvalue %absolute.array.int32.1 %values.value13, 2
  %index.value17 = load i32, ptr %index, align 4
  %array.index.wide18 = sext i32 %index.value17 to i64
  %array.index.valid19 = icmp ult i64 %array.index.wide18, %array.dimension16
  br i1 %array.index.valid19, label %array.bounds.success20, label %array.bounds.failure21, !prof !0

array.bounds.failure:                             ; preds = %error.continue
  %0 = call i32 @puts(ptr @array.bounds.message.1)
  call void @exit(i32 1)
  unreachable

array.bounds.success20:                           ; preds = %array.bounds.success
  %array.element.address22 = getelementptr inbounds i32, ptr %array.data14, i32 %index.value17
  %value.value = load i32, ptr %value2, align 4
  store i32 %value.value, ptr %array.element.address22, align 4
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value = load %absolute.array.int32.1, ptr %states.address, align 8
  %array.data23 = extractvalue %absolute.array.int32.1 %states.value, 0
  %array.owner24 = extractvalue %absolute.array.int32.1 %states.value, 1
  %array.dimension25 = extractvalue %absolute.array.int32.1 %states.value, 2
  %states.address26 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value27 = load %absolute.array.int32.1, ptr %states.address26, align 8
  %array.data28 = extractvalue %absolute.array.int32.1 %states.value27, 0
  %array.owner29 = extractvalue %absolute.array.int32.1 %states.value27, 1
  %array.dimension30 = extractvalue %absolute.array.int32.1 %states.value27, 2
  %index.value31 = load i32, ptr %index, align 4
  %array.index.wide32 = sext i32 %index.value31 to i64
  %array.index.valid33 = icmp ult i64 %array.index.wide32, %array.dimension30
  br i1 %array.index.valid33, label %array.bounds.success34, label %array.bounds.failure35, !prof !0

array.bounds.failure21:                           ; preds = %array.bounds.success
  %1 = call i32 @puts(ptr @array.bounds.message.2)
  call void @exit(i32 1)
  unreachable

array.bounds.success34:                           ; preds = %array.bounds.success20
  %array.element.address36 = getelementptr inbounds i32, ptr %array.data28, i32 %index.value31
  store i32 1, ptr %array.element.address36, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  %_count.address37 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  %_count.value = load i32, ptr %_count.address37, align 4
  %add = add i32 %_count.value, 1
  store i32 %add, ptr %_count.address, align 4
  ret void

array.bounds.failure35:                           ; preds = %array.bounds.success20
  %2 = call i32 @puts(ptr @array.bounds.message.3)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.HashMap<int32,int32>.clearStates"(ptr %this) {
entry:
  %index = alloca i32, align 4
  store i32 0, ptr %index, align 4
  br label %while.condition

while.condition:                                  ; preds = %array.bounds.success, %entry
  %index.value = load i32, ptr %index, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %less = icmp slt i32 %index.value, %_capacity.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value = load %absolute.array.int32.1, ptr %states.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %states.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %states.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %states.value, 2
  %states.address1 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value2 = load %absolute.array.int32.1, ptr %states.address1, align 8
  %array.data3 = extractvalue %absolute.array.int32.1 %states.value2, 0
  %array.owner4 = extractvalue %absolute.array.int32.1 %states.value2, 1
  %array.dimension5 = extractvalue %absolute.array.int32.1 %states.value2, 2
  %index.value6 = load i32, ptr %index, align 4
  %array.index.wide = sext i32 %index.value6 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension5
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  ret void

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data3, i32 %index.value6
  store i32 0, ptr %array.element.address, align 4
  %index.value7 = load i32, ptr %index, align 4
  %add = add i32 %index.value7, 1
  store i32 %add, ptr %index, align 4
  br label %while.condition

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.4)
  call void @exit(i32 1)
  unreachable
}

define i32 @"std.collections.HashMap<int32,int32>.bucket$int32"(ptr %this, i32 %key) {
entry:
  %slot = alloca i64, align 8
  %rawHash = alloca i64, align 8
  %hash = alloca ptr, align 8
  %key1 = alloca i32, align 4
  store i32 %key, ptr %key1, align 4
  %hashFunction.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 7
  %hashFunction.value = load ptr, ptr %hashFunction.address, align 8
  call void @__absolute.closure.retain(ptr %hashFunction.value)
  store ptr %hashFunction.value, ptr %hash, align 8
  %hash.value = load ptr, ptr %hash, align 8
  %function.value.nonnull = icmp ne ptr %hash.value, null
  br i1 %function.value.nonnull, label %function.null.success, label %function.null.failure, !prof !0

function.null.success:                            ; preds = %entry
  %closure.invoke.address = getelementptr inbounds %absolute.closure, ptr %hash.value, i32 0, i32 1
  %closure.invoke = load ptr, ptr %closure.invoke.address, align 8
  %closure.environment.address = getelementptr inbounds %absolute.closure, ptr %hash.value, i32 0, i32 2
  %closure.environment = load ptr, ptr %closure.environment.address, align 8
  %key.value = load i32, ptr %key1, align 4
  %function.value.result = call i64 %closure.invoke(ptr %closure.environment, i32 %key.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

function.null.failure:                            ; preds = %entry
  %0 = call i32 @puts(ptr @function.null.message)
  call void @exit(i32 1)
  unreachable

error.propagate:                                  ; preds = %function.null.success
  %closure.cleanup.value = load ptr, ptr %hash, align 8
  call void @__absolute.closure.release(ptr %closure.cleanup.value)
  store ptr null, ptr %hash, align 8
  ret i32 0

error.continue:                                   ; preds = %function.null.success
  store i64 %function.value.result, ptr %rawHash, align 4
  %rawHash.value = load i64, ptr %rawHash, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %int.cast = sext i32 %_capacity.value to i64
  %rem = srem i64 %rawHash.value, %int.cast
  store i64 %rem, ptr %slot, align 4
  %slot.value = load i64, ptr %slot, align 4
  %less = icmp slt i64 %slot.value, 0
  br i1 %less, label %if.body, label %if.end

if.end:                                           ; preds = %if.body, %error.continue
  %slot.value3 = load i64, ptr %slot, align 4
  %int.cast4 = trunc i64 %slot.value3 to i32
  %closure.cleanup.value5 = load ptr, ptr %hash, align 8
  call void @__absolute.closure.release(ptr %closure.cleanup.value5)
  store ptr null, ptr %hash, align 8
  ret i32 %int.cast4

if.body:                                          ; preds = %error.continue
  %slot.value2 = load i64, ptr %slot, align 4
  %negate = sub i64 0, %slot.value2
  store i64 %negate, ptr %slot, align 4
  br label %if.end
}

define i1 @"std.collections.HashMap<int32,int32>.keysEqual$int32$int32"(ptr %this, i32 %left, i32 %right) {
entry:
  %equals = alloca ptr, align 8
  %right2 = alloca i32, align 4
  %left1 = alloca i32, align 4
  store i32 %left, ptr %left1, align 4
  store i32 %right, ptr %right2, align 4
  %equalityFunction.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 8
  %equalityFunction.value = load ptr, ptr %equalityFunction.address, align 8
  call void @__absolute.closure.retain(ptr %equalityFunction.value)
  store ptr %equalityFunction.value, ptr %equals, align 8
  %equals.value = load ptr, ptr %equals, align 8
  %function.value.nonnull = icmp ne ptr %equals.value, null
  br i1 %function.value.nonnull, label %function.null.success, label %function.null.failure, !prof !0

function.null.success:                            ; preds = %entry
  %closure.invoke.address = getelementptr inbounds %absolute.closure, ptr %equals.value, i32 0, i32 1
  %closure.invoke = load ptr, ptr %closure.invoke.address, align 8
  %closure.environment.address = getelementptr inbounds %absolute.closure, ptr %equals.value, i32 0, i32 2
  %closure.environment = load ptr, ptr %closure.environment.address, align 8
  %left.value = load i32, ptr %left1, align 4
  %right.value = load i32, ptr %right2, align 4
  %function.value.result = call i1 %closure.invoke(ptr %closure.environment, i32 %left.value, i32 %right.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

function.null.failure:                            ; preds = %entry
  %0 = call i32 @puts(ptr @function.null.message.5)
  call void @exit(i32 1)
  unreachable

error.propagate:                                  ; preds = %function.null.success
  %closure.cleanup.value = load ptr, ptr %equals, align 8
  call void @__absolute.closure.release(ptr %closure.cleanup.value)
  store ptr null, ptr %equals, align 8
  ret i1 false

error.continue:                                   ; preds = %function.null.success
  %closure.cleanup.value3 = load ptr, ptr %equals, align 8
  call void @__absolute.closure.release(ptr %closure.cleanup.value3)
  store ptr null, ptr %equals, align 8
  ret i1 %function.value.result
}

define i32 @"std.collections.HashMap<int32,int32>.findIndex$int32"(ptr %this, i32 %key) {
entry:
  %scanned = alloca i32, align 4
  %index = alloca i32, align 4
  %key1 = alloca i32, align 4
  store i32 %key, ptr %key1, align 4
  %key.value = load i32, ptr %key1, align 4
  %bucket.result = call i32 @"std.collections.HashMap<int32,int32>.bucket$int32"(ptr %this, i32 %key.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  ret i32 0

error.continue:                                   ; preds = %entry
  store i32 %bucket.result, ptr %index, align 4
  store i32 0, ptr %scanned, align 4
  br label %while.condition

while.condition:                                  ; preds = %if.end7, %error.continue
  %scanned.value = load i32, ptr %scanned, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %less = icmp slt i32 %scanned.value, %_capacity.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value = load %absolute.array.int32.1, ptr %states.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %states.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %states.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %states.value, 2
  %states.address2 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value3 = load %absolute.array.int32.1, ptr %states.address2, align 8
  %array.data4 = extractvalue %absolute.array.int32.1 %states.value3, 0
  %array.owner5 = extractvalue %absolute.array.int32.1 %states.value3, 1
  %array.dimension6 = extractvalue %absolute.array.int32.1 %states.value3, 2
  %index.value = load i32, ptr %index, align 4
  %array.index.wide = sext i32 %index.value to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension6
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  ret i32 -1

if.end:                                           ; preds = %array.bounds.success
  %states.address8 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value9 = load %absolute.array.int32.1, ptr %states.address8, align 8
  %array.data10 = extractvalue %absolute.array.int32.1 %states.value9, 0
  %array.owner11 = extractvalue %absolute.array.int32.1 %states.value9, 1
  %array.dimension12 = extractvalue %absolute.array.int32.1 %states.value9, 2
  %states.address13 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value14 = load %absolute.array.int32.1, ptr %states.address13, align 8
  %array.data15 = extractvalue %absolute.array.int32.1 %states.value14, 0
  %array.owner16 = extractvalue %absolute.array.int32.1 %states.value14, 1
  %array.dimension17 = extractvalue %absolute.array.int32.1 %states.value14, 2
  %index.value18 = load i32, ptr %index, align 4
  %array.index.wide19 = sext i32 %index.value18 to i64
  %array.index.valid20 = icmp ult i64 %array.index.wide19, %array.dimension17
  br i1 %array.index.valid20, label %array.bounds.success21, label %array.bounds.failure22, !prof !0

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data4, i32 %index.value
  %array.element = load i32, ptr %array.element.address, align 4
  %equal = icmp eq i32 %array.element, 0
  br i1 %equal, label %if.body, label %if.end

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.6)
  call void @exit(i32 1)
  unreachable

if.body:                                          ; preds = %array.bounds.success
  ret i32 -1

if.end7:                                          ; preds = %error.continue44
  %index.value47 = load i32, ptr %index, align 4
  %add = add i32 %index.value47, 1
  %_capacity.address48 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value49 = load i32, ptr %_capacity.address48, align 4
  %rem = srem i32 %add, %_capacity.value49
  store i32 %rem, ptr %index, align 4
  %scanned.value50 = load i32, ptr %scanned, align 4
  %add51 = add i32 %scanned.value50, 1
  store i32 %add51, ptr %scanned, align 4
  br label %while.condition

array.bounds.success21:                           ; preds = %if.end
  %array.element.address23 = getelementptr inbounds i32, ptr %array.data15, i32 %index.value18
  %array.element24 = load i32, ptr %array.element.address23, align 4
  %equal25 = icmp eq i32 %array.element24, 1
  %keys.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %keys.value = load %absolute.array.int32.1, ptr %keys.address, align 8
  %array.data26 = extractvalue %absolute.array.int32.1 %keys.value, 0
  %array.owner27 = extractvalue %absolute.array.int32.1 %keys.value, 1
  %array.dimension28 = extractvalue %absolute.array.int32.1 %keys.value, 2
  %keys.address29 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %keys.value30 = load %absolute.array.int32.1, ptr %keys.address29, align 8
  %array.data31 = extractvalue %absolute.array.int32.1 %keys.value30, 0
  %array.owner32 = extractvalue %absolute.array.int32.1 %keys.value30, 1
  %array.dimension33 = extractvalue %absolute.array.int32.1 %keys.value30, 2
  %index.value34 = load i32, ptr %index, align 4
  %array.index.wide35 = sext i32 %index.value34 to i64
  %array.index.valid36 = icmp ult i64 %array.index.wide35, %array.dimension33
  br i1 %array.index.valid36, label %array.bounds.success37, label %array.bounds.failure38, !prof !0

array.bounds.failure22:                           ; preds = %if.end
  %1 = call i32 @puts(ptr @array.bounds.message.7)
  call void @exit(i32 1)
  unreachable

array.bounds.success37:                           ; preds = %array.bounds.success21
  %array.element.address39 = getelementptr inbounds i32, ptr %array.data31, i32 %index.value34
  %array.element40 = load i32, ptr %array.element.address39, align 4
  %key.value41 = load i32, ptr %key1, align 4
  %keysEqual.result = call i1 @"std.collections.HashMap<int32,int32>.keysEqual$int32$int32"(ptr %this, i32 %array.element40, i32 %key.value41)
  %error.pending42 = call i1 @absolute_error_pending()
  br i1 %error.pending42, label %error.propagate43, label %error.continue44

array.bounds.failure38:                           ; preds = %array.bounds.success21
  %2 = call i32 @puts(ptr @array.bounds.message.8)
  call void @exit(i32 1)
  unreachable

error.propagate43:                                ; preds = %array.bounds.success37
  ret i32 0

error.continue44:                                 ; preds = %array.bounds.success37
  %logical.and = and i1 %equal25, %keysEqual.result
  br i1 %logical.and, label %if.body45, label %if.end7

if.body45:                                        ; preds = %error.continue44
  %index.value46 = load i32, ptr %index, align 4
  ret i32 %index.value46
}

define i32 @"std.collections.HashMap<int32,int32>.findInsertionIndex$int32"(ptr %this, i32 %key) {
entry:
  %scanned = alloca i32, align 4
  %firstTombstone = alloca i32, align 4
  %index = alloca i32, align 4
  %key1 = alloca i32, align 4
  store i32 %key, ptr %key1, align 4
  %key.value = load i32, ptr %key1, align 4
  %bucket.result = call i32 @"std.collections.HashMap<int32,int32>.bucket$int32"(ptr %this, i32 %key.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  ret i32 0

error.continue:                                   ; preds = %entry
  store i32 %bucket.result, ptr %index, align 4
  store i32 -1, ptr %firstTombstone, align 4
  store i32 0, ptr %scanned, align 4
  br label %while.condition

while.condition:                                  ; preds = %if.end9, %error.continue
  %scanned.value = load i32, ptr %scanned, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %less = icmp slt i32 %scanned.value, %_capacity.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value = load %absolute.array.int32.1, ptr %states.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %states.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %states.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %states.value, 2
  %states.address2 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value3 = load %absolute.array.int32.1, ptr %states.address2, align 8
  %array.data4 = extractvalue %absolute.array.int32.1 %states.value3, 0
  %array.owner5 = extractvalue %absolute.array.int32.1 %states.value3, 1
  %array.dimension6 = extractvalue %absolute.array.int32.1 %states.value3, 2
  %index.value = load i32, ptr %index, align 4
  %array.index.wide = sext i32 %index.value to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension6
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %firstTombstone.value37 = load i32, ptr %firstTombstone, align 4
  ret i32 %firstTombstone.value37

if.end:                                           ; preds = %array.bounds.success
  %states.address10 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value11 = load %absolute.array.int32.1, ptr %states.address10, align 8
  %array.data12 = extractvalue %absolute.array.int32.1 %states.value11, 0
  %array.owner13 = extractvalue %absolute.array.int32.1 %states.value11, 1
  %array.dimension14 = extractvalue %absolute.array.int32.1 %states.value11, 2
  %states.address15 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value16 = load %absolute.array.int32.1, ptr %states.address15, align 8
  %array.data17 = extractvalue %absolute.array.int32.1 %states.value16, 0
  %array.owner18 = extractvalue %absolute.array.int32.1 %states.value16, 1
  %array.dimension19 = extractvalue %absolute.array.int32.1 %states.value16, 2
  %index.value20 = load i32, ptr %index, align 4
  %array.index.wide21 = sext i32 %index.value20 to i64
  %array.index.valid22 = icmp ult i64 %array.index.wide21, %array.dimension19
  br i1 %array.index.valid22, label %array.bounds.success23, label %array.bounds.failure24, !prof !0

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data4, i32 %index.value
  %array.element = load i32, ptr %array.element.address, align 4
  %equal = icmp eq i32 %array.element, 0
  br i1 %equal, label %if.body, label %if.end

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.9)
  call void @exit(i32 1)
  unreachable

if.body:                                          ; preds = %array.bounds.success
  %firstTombstone.value = load i32, ptr %firstTombstone, align 4
  %greater.equal = icmp sge i32 %firstTombstone.value, 0
  br i1 %greater.equal, label %ternary.true, label %ternary.false

ternary.true:                                     ; preds = %if.body
  %firstTombstone.value7 = load i32, ptr %firstTombstone, align 4
  br label %ternary.end

ternary.false:                                    ; preds = %if.body
  %index.value8 = load i32, ptr %index, align 4
  br label %ternary.end

ternary.end:                                      ; preds = %ternary.false, %ternary.true
  %ternary.result = phi i32 [ %firstTombstone.value7, %ternary.true ], [ %index.value8, %ternary.false ]
  ret i32 %ternary.result

if.end9:                                          ; preds = %if.body30, %array.bounds.success23
  %index.value32 = load i32, ptr %index, align 4
  %add = add i32 %index.value32, 1
  %_capacity.address33 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value34 = load i32, ptr %_capacity.address33, align 4
  %rem = srem i32 %add, %_capacity.value34
  store i32 %rem, ptr %index, align 4
  %scanned.value35 = load i32, ptr %scanned, align 4
  %add36 = add i32 %scanned.value35, 1
  store i32 %add36, ptr %scanned, align 4
  br label %while.condition

array.bounds.success23:                           ; preds = %if.end
  %array.element.address25 = getelementptr inbounds i32, ptr %array.data17, i32 %index.value20
  %array.element26 = load i32, ptr %array.element.address25, align 4
  %equal27 = icmp eq i32 %array.element26, 2
  %firstTombstone.value28 = load i32, ptr %firstTombstone, align 4
  %less29 = icmp slt i32 %firstTombstone.value28, 0
  %logical.and = and i1 %equal27, %less29
  br i1 %logical.and, label %if.body30, label %if.end9

array.bounds.failure24:                           ; preds = %if.end
  %1 = call i32 @puts(ptr @array.bounds.message.10)
  call void @exit(i32 1)
  unreachable

if.body30:                                        ; preds = %array.bounds.success23
  %index.value31 = load i32, ptr %index, align 4
  store i32 %index.value31, ptr %firstTombstone, align 4
  br label %if.end9
}

define void @"std.collections.HashMap<int32,int32>.rehash$int32"(ptr %this, i32 %newCapacity) {
entry:
  %index = alloca i32, align 4
  %previousCapacity = alloca i32, align 4
  %newCapacity1 = alloca i32, align 4
  store i32 %newCapacity, ptr %newCapacity1, align 4
  %keys.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %keys.value = load %absolute.array.int32.1, ptr %keys.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %keys.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %keys.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %keys.value, 2
  %copy.element.count = mul i64 1, %array.dimension
  %copy.byte.count = mul i64 %copy.element.count, 4
  %copy.data = call ptr @malloc(i64 %copy.byte.count)
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %copy.data, ptr align 1 %array.data, i64 %copy.byte.count, i1 false)
  %array.data2 = insertvalue %absolute.array.int32.1 undef, ptr %copy.data, 0
  %array.owner3 = insertvalue %absolute.array.int32.1 %array.data2, ptr %copy.data, 1
  %array.dimension4 = insertvalue %absolute.array.int32.1 %array.owner3, i64 %array.dimension, 2
  %array.data5 = extractvalue %absolute.array.int32.1 %array.dimension4, 0
  %array.owner6 = extractvalue %absolute.array.int32.1 %array.dimension4, 1
  %array.dimension7 = extractvalue %absolute.array.int32.1 %array.dimension4, 2
  %array.data8 = insertvalue %absolute.array.int32.1 undef, ptr %array.data5, 0
  %array.owner9 = insertvalue %absolute.array.int32.1 %array.data8, ptr %array.owner6, 1
  %array.dimension10 = insertvalue %absolute.array.int32.1 %array.owner9, i64 %array.dimension7, 2
  %values.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value = load %absolute.array.int32.1, ptr %values.address, align 8
  %array.data11 = extractvalue %absolute.array.int32.1 %values.value, 0
  %array.owner12 = extractvalue %absolute.array.int32.1 %values.value, 1
  %array.dimension13 = extractvalue %absolute.array.int32.1 %values.value, 2
  %copy.element.count14 = mul i64 1, %array.dimension13
  %copy.byte.count15 = mul i64 %copy.element.count14, 4
  %copy.data16 = call ptr @malloc(i64 %copy.byte.count15)
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %copy.data16, ptr align 1 %array.data11, i64 %copy.byte.count15, i1 false)
  %array.data17 = insertvalue %absolute.array.int32.1 undef, ptr %copy.data16, 0
  %array.owner18 = insertvalue %absolute.array.int32.1 %array.data17, ptr %copy.data16, 1
  %array.dimension19 = insertvalue %absolute.array.int32.1 %array.owner18, i64 %array.dimension13, 2
  %array.data20 = extractvalue %absolute.array.int32.1 %array.dimension19, 0
  %array.owner21 = extractvalue %absolute.array.int32.1 %array.dimension19, 1
  %array.dimension22 = extractvalue %absolute.array.int32.1 %array.dimension19, 2
  %array.data23 = insertvalue %absolute.array.int32.1 undef, ptr %array.data20, 0
  %array.owner24 = insertvalue %absolute.array.int32.1 %array.data23, ptr %array.owner21, 1
  %array.dimension25 = insertvalue %absolute.array.int32.1 %array.owner24, i64 %array.dimension22, 2
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value = load %absolute.array.int32.1, ptr %states.address, align 8
  %array.data26 = extractvalue %absolute.array.int32.1 %states.value, 0
  %array.owner27 = extractvalue %absolute.array.int32.1 %states.value, 1
  %array.dimension28 = extractvalue %absolute.array.int32.1 %states.value, 2
  %copy.element.count29 = mul i64 1, %array.dimension28
  %copy.byte.count30 = mul i64 %copy.element.count29, 4
  %copy.data31 = call ptr @malloc(i64 %copy.byte.count30)
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %copy.data31, ptr align 1 %array.data26, i64 %copy.byte.count30, i1 false)
  %array.data32 = insertvalue %absolute.array.int32.1 undef, ptr %copy.data31, 0
  %array.owner33 = insertvalue %absolute.array.int32.1 %array.data32, ptr %copy.data31, 1
  %array.dimension34 = insertvalue %absolute.array.int32.1 %array.owner33, i64 %array.dimension28, 2
  %array.data35 = extractvalue %absolute.array.int32.1 %array.dimension34, 0
  %array.owner36 = extractvalue %absolute.array.int32.1 %array.dimension34, 1
  %array.dimension37 = extractvalue %absolute.array.int32.1 %array.dimension34, 2
  %array.data38 = insertvalue %absolute.array.int32.1 undef, ptr %array.data35, 0
  %array.owner39 = insertvalue %absolute.array.int32.1 %array.data38, ptr %array.owner36, 1
  %array.dimension40 = insertvalue %absolute.array.int32.1 %array.owner39, i64 %array.dimension37, 2
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  store i32 %_capacity.value, ptr %previousCapacity, align 4
  %_capacity.address41 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %newCapacity.value = load i32, ptr %newCapacity1, align 4
  store i32 %newCapacity.value, ptr %_capacity.address41, align 4
  %keys.address42 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %_capacity.address43 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value44 = load i32, ptr %_capacity.address43, align 4
  %int.cast = sext i32 %_capacity.value44 to i64
  %array.alloc.bytes = mul i64 %int.cast, 4
  %array.data.alloc = call ptr @malloc(i64 %array.alloc.bytes)
  %array.data45 = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc, 0
  %array.owner46 = insertvalue %absolute.array.int32.1 %array.data45, ptr %array.data.alloc, 1
  %array.dimension47 = insertvalue %absolute.array.int32.1 %array.owner46, i64 %int.cast, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %keys.address42, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %keys.address42, align 8
  store %absolute.array.int32.1 %array.dimension47, ptr %keys.address42, align 8
  %values.address48 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %_capacity.address49 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value50 = load i32, ptr %_capacity.address49, align 4
  %int.cast51 = sext i32 %_capacity.value50 to i64
  %array.alloc.bytes52 = mul i64 %int.cast51, 4
  %array.data.alloc53 = call ptr @malloc(i64 %array.alloc.bytes52)
  %array.data54 = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc53, 0
  %array.owner55 = insertvalue %absolute.array.int32.1 %array.data54, ptr %array.data.alloc53, 1
  %array.dimension56 = insertvalue %absolute.array.int32.1 %array.owner55, i64 %int.cast51, 2
  %field.cleanup.array57 = load %absolute.array.int32.1, ptr %values.address48, align 8
  %field.cleanup.array.owner58 = extractvalue %absolute.array.int32.1 %field.cleanup.array57, 1
  call void @free(ptr %field.cleanup.array.owner58)
  store %absolute.array.int32.1 zeroinitializer, ptr %values.address48, align 8
  store %absolute.array.int32.1 %array.dimension56, ptr %values.address48, align 8
  %states.address59 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %_capacity.address60 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value61 = load i32, ptr %_capacity.address60, align 4
  %int.cast62 = sext i32 %_capacity.value61 to i64
  %array.alloc.bytes63 = mul i64 %int.cast62, 4
  %array.data.alloc64 = call ptr @malloc(i64 %array.alloc.bytes63)
  %array.data65 = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc64, 0
  %array.owner66 = insertvalue %absolute.array.int32.1 %array.data65, ptr %array.data.alloc64, 1
  %array.dimension67 = insertvalue %absolute.array.int32.1 %array.owner66, i64 %int.cast62, 2
  %field.cleanup.array68 = load %absolute.array.int32.1, ptr %states.address59, align 8
  %field.cleanup.array.owner69 = extractvalue %absolute.array.int32.1 %field.cleanup.array68, 1
  call void @free(ptr %field.cleanup.array.owner69)
  store %absolute.array.int32.1 zeroinitializer, ptr %states.address59, align 8
  store %absolute.array.int32.1 %array.dimension67, ptr %states.address59, align 8
  call void @"std.collections.HashMap<int32,int32>.clearStates"(ptr %this)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  call void @free(ptr %array.owner6)
  call void @free(ptr %array.owner21)
  call void @free(ptr %array.owner36)
  ret void

error.continue:                                   ; preds = %entry
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  store i32 0, ptr %_count.address, align 4
  %tombstoneCount.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 5
  store i32 0, ptr %tombstoneCount.address, align 4
  store i32 0, ptr %index, align 4
  br label %while.condition

while.condition:                                  ; preds = %if.end, %error.continue
  %index.value = load i32, ptr %index, align 4
  %previousCapacity.value = load i32, ptr %previousCapacity, align 4
  %less = icmp slt i32 %index.value, %previousCapacity.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %index.value70 = load i32, ptr %index, align 4
  %array.index.wide = sext i32 %index.value70 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension37
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  call void @free(ptr %array.owner6)
  call void @free(ptr %array.owner21)
  call void @free(ptr %array.owner36)
  ret void

if.end:                                           ; preds = %error.continue87, %array.bounds.success
  %index.value88 = load i32, ptr %index, align 4
  %add = add i32 %index.value88, 1
  store i32 %add, ptr %index, align 4
  br label %while.condition

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data35, i32 %index.value70
  %array.element = load i32, ptr %array.element.address, align 4
  %equal = icmp eq i32 %array.element, 1
  br i1 %equal, label %if.body, label %if.end

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.11)
  call void @exit(i32 1)
  unreachable

if.body:                                          ; preds = %array.bounds.success
  %index.value71 = load i32, ptr %index, align 4
  %array.index.wide72 = sext i32 %index.value71 to i64
  %array.index.valid73 = icmp ult i64 %array.index.wide72, %array.dimension7
  br i1 %array.index.valid73, label %array.bounds.success74, label %array.bounds.failure75, !prof !0

array.bounds.success74:                           ; preds = %if.body
  %array.element.address76 = getelementptr inbounds i32, ptr %array.data5, i32 %index.value71
  %array.element77 = load i32, ptr %array.element.address76, align 4
  %index.value78 = load i32, ptr %index, align 4
  %array.index.wide79 = sext i32 %index.value78 to i64
  %array.index.valid80 = icmp ult i64 %array.index.wide79, %array.dimension22
  br i1 %array.index.valid80, label %array.bounds.success81, label %array.bounds.failure82, !prof !0

array.bounds.failure75:                           ; preds = %if.body
  %1 = call i32 @puts(ptr @array.bounds.message.12)
  call void @exit(i32 1)
  unreachable

array.bounds.success81:                           ; preds = %array.bounds.success74
  %array.element.address83 = getelementptr inbounds i32, ptr %array.data20, i32 %index.value78
  %array.element84 = load i32, ptr %array.element.address83, align 4
  call void @"std.collections.HashMap<int32,int32>.insertRehashed$int32$int32"(ptr %this, i32 %array.element77, i32 %array.element84)
  %error.pending85 = call i1 @absolute_error_pending()
  br i1 %error.pending85, label %error.propagate86, label %error.continue87

array.bounds.failure82:                           ; preds = %array.bounds.success74
  %2 = call i32 @puts(ptr @array.bounds.message.13)
  call void @exit(i32 1)
  unreachable

error.propagate86:                                ; preds = %array.bounds.success81
  call void @free(ptr %array.owner6)
  call void @free(ptr %array.owner21)
  call void @free(ptr %array.owner36)
  ret void

error.continue87:                                 ; preds = %array.bounds.success81
  br label %if.end
}

define %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" @"std.collections.HashMap<int32,int32>.toArray"(ptr %this) {
entry:
  %resultIndex = alloca i32, align 4
  %sourceIndex = alloca i32, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  %_count.value = load i32, ptr %_count.address, align 4
  %int.cast = sext i32 %_count.value to i64
  %array.alloc.bytes = mul i64 %int.cast, 8
  %array.data.alloc = call ptr @malloc(i64 %array.alloc.bytes)
  %array.data = insertvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" undef, ptr %array.data.alloc, 0
  %array.owner = insertvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.data, ptr %array.data.alloc, 1
  %array.dimension = insertvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.owner, i64 %int.cast, 2
  %array.data1 = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.dimension, 0
  %array.owner2 = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.dimension, 1
  %array.dimension3 = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.dimension, 2
  %array.data4 = insertvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" undef, ptr %array.data1, 0
  %array.owner5 = insertvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.data4, ptr %array.owner2, 1
  %array.dimension6 = insertvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.owner5, i64 %array.dimension3, 2
  store i32 0, ptr %sourceIndex, align 4
  store i32 0, ptr %resultIndex, align 4
  br label %while.condition

while.condition:                                  ; preds = %if.end, %entry
  %sourceIndex.value = load i32, ptr %sourceIndex, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %less = icmp slt i32 %sourceIndex.value, %_capacity.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value = load %absolute.array.int32.1, ptr %states.address, align 8
  %array.data7 = extractvalue %absolute.array.int32.1 %states.value, 0
  %array.owner8 = extractvalue %absolute.array.int32.1 %states.value, 1
  %array.dimension9 = extractvalue %absolute.array.int32.1 %states.value, 2
  %states.address10 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value11 = load %absolute.array.int32.1, ptr %states.address10, align 8
  %array.data12 = extractvalue %absolute.array.int32.1 %states.value11, 0
  %array.owner13 = extractvalue %absolute.array.int32.1 %states.value11, 1
  %array.dimension14 = extractvalue %absolute.array.int32.1 %states.value11, 2
  %sourceIndex.value15 = load i32, ptr %sourceIndex, align 4
  %array.index.wide = sext i32 %sourceIndex.value15 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension14
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %array.data54 = insertvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" undef, ptr %array.data1, 0
  %array.owner55 = insertvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.data54, ptr %array.owner2, 1
  %array.dimension56 = insertvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.owner55, i64 %array.dimension3, 2
  ret %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %array.dimension56

if.end:                                           ; preds = %error.continue, %array.bounds.success
  %sourceIndex.value52 = load i32, ptr %sourceIndex, align 4
  %add53 = add i32 %sourceIndex.value52, 1
  store i32 %add53, ptr %sourceIndex, align 4
  br label %while.condition

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data12, i32 %sourceIndex.value15
  %array.element = load i32, ptr %array.element.address, align 4
  %equal = icmp eq i32 %array.element, 1
  br i1 %equal, label %if.body, label %if.end

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.14)
  call void @exit(i32 1)
  unreachable

if.body:                                          ; preds = %array.bounds.success
  %resultIndex.value = load i32, ptr %resultIndex, align 4
  %array.index.wide16 = sext i32 %resultIndex.value to i64
  %array.index.valid17 = icmp ult i64 %array.index.wide16, %array.dimension3
  br i1 %array.index.valid17, label %array.bounds.success18, label %array.bounds.failure19, !prof !0

array.bounds.success18:                           ; preds = %if.body
  %array.element.address20 = getelementptr inbounds %"absolute.struct.std.collections.HashKeyValuePair<int32,int32>", ptr %array.data1, i32 %resultIndex.value
  %keys.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %keys.value = load %absolute.array.int32.1, ptr %keys.address, align 8
  %array.data21 = extractvalue %absolute.array.int32.1 %keys.value, 0
  %array.owner22 = extractvalue %absolute.array.int32.1 %keys.value, 1
  %array.dimension23 = extractvalue %absolute.array.int32.1 %keys.value, 2
  %keys.address24 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %keys.value25 = load %absolute.array.int32.1, ptr %keys.address24, align 8
  %array.data26 = extractvalue %absolute.array.int32.1 %keys.value25, 0
  %array.owner27 = extractvalue %absolute.array.int32.1 %keys.value25, 1
  %array.dimension28 = extractvalue %absolute.array.int32.1 %keys.value25, 2
  %sourceIndex.value29 = load i32, ptr %sourceIndex, align 4
  %array.index.wide30 = sext i32 %sourceIndex.value29 to i64
  %array.index.valid31 = icmp ult i64 %array.index.wide30, %array.dimension28
  br i1 %array.index.valid31, label %array.bounds.success32, label %array.bounds.failure33, !prof !0

array.bounds.failure19:                           ; preds = %if.body
  %1 = call i32 @puts(ptr @array.bounds.message.15)
  call void @exit(i32 1)
  unreachable

array.bounds.success32:                           ; preds = %array.bounds.success18
  %array.element.address34 = getelementptr inbounds i32, ptr %array.data26, i32 %sourceIndex.value29
  %array.element35 = load i32, ptr %array.element.address34, align 4
  %values.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value = load %absolute.array.int32.1, ptr %values.address, align 8
  %array.data36 = extractvalue %absolute.array.int32.1 %values.value, 0
  %array.owner37 = extractvalue %absolute.array.int32.1 %values.value, 1
  %array.dimension38 = extractvalue %absolute.array.int32.1 %values.value, 2
  %values.address39 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value40 = load %absolute.array.int32.1, ptr %values.address39, align 8
  %array.data41 = extractvalue %absolute.array.int32.1 %values.value40, 0
  %array.owner42 = extractvalue %absolute.array.int32.1 %values.value40, 1
  %array.dimension43 = extractvalue %absolute.array.int32.1 %values.value40, 2
  %sourceIndex.value44 = load i32, ptr %sourceIndex, align 4
  %array.index.wide45 = sext i32 %sourceIndex.value44 to i64
  %array.index.valid46 = icmp ult i64 %array.index.wide45, %array.dimension43
  br i1 %array.index.valid46, label %array.bounds.success47, label %array.bounds.failure48, !prof !0

array.bounds.failure33:                           ; preds = %array.bounds.success18
  %2 = call i32 @puts(ptr @array.bounds.message.16)
  call void @exit(i32 1)
  unreachable

array.bounds.success47:                           ; preds = %array.bounds.success32
  %array.element.address49 = getelementptr inbounds i32, ptr %array.data41, i32 %sourceIndex.value44
  %array.element50 = load i32, ptr %array.element.address49, align 4
  call void @"std.collections.HashKeyValuePair<int32,int32>.initialize$int32$int32"(ptr %array.element.address20, i32 %array.element35, i32 %array.element50)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

array.bounds.failure48:                           ; preds = %array.bounds.success32
  %3 = call i32 @puts(ptr @array.bounds.message.17)
  call void @exit(i32 1)
  unreachable

error.propagate:                                  ; preds = %array.bounds.success47
  ret %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" zeroinitializer

error.continue:                                   ; preds = %array.bounds.success47
  %resultIndex.value51 = load i32, ptr %resultIndex, align 4
  %add = add i32 %resultIndex.value51, 1
  store i32 %add, ptr %resultIndex, align 4
  br label %if.end
}

define void @"std.collections.HashMap<int32,int32>.ensureRoomForNewEntry"(ptr %this) {
entry:
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  %_count.value = load i32, ptr %_count.address, align 4
  %tombstoneCount.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 5
  %tombstoneCount.value = load i32, ptr %tombstoneCount.address, align 4
  %add = add i32 %_count.value, %tombstoneCount.value
  %add1 = add i32 %add, 1
  %mul = mul i32 %add1, 10
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %mul2 = mul i32 %_capacity.value, 7
  %greater.equal = icmp sge i32 %mul, %mul2
  br i1 %greater.equal, label %if.body, label %if.end

if.end:                                           ; preds = %error.continue, %entry
  ret void

if.body:                                          ; preds = %entry
  %_capacity.address3 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value4 = load i32, ptr %_capacity.address3, align 4
  %mul5 = mul i32 %_capacity.value4, 2
  call void @"std.collections.HashMap<int32,int32>.rehash$int32"(ptr %this, i32 %mul5)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %if.body
  ret void

error.continue:                                   ; preds = %if.body
  br label %if.end
}

define i32 @"std.collections.HashMap<int32,int32>.__absolute_property_get_count"(ptr %this) {
entry:
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  %_count.value = load i32, ptr %_count.address, align 4
  ret i32 %_count.value
}

define void @"std.collections.HashMap<int32,int32>.put$int32$int32"(ptr %this, i32 %key, i32 %value) {
entry:
  %index = alloca i32, align 4
  %existing = alloca i32, align 4
  %value2 = alloca i32, align 4
  %key1 = alloca i32, align 4
  store i32 %key, ptr %key1, align 4
  store i32 %value, ptr %value2, align 4
  %key.value = load i32, ptr %key1, align 4
  %findIndex.result = call i32 @"std.collections.HashMap<int32,int32>.findIndex$int32"(ptr %this, i32 %key.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  ret void

error.continue:                                   ; preds = %entry
  store i32 %findIndex.result, ptr %existing, align 4
  %existing.value = load i32, ptr %existing, align 4
  %greater.equal = icmp sge i32 %existing.value, 0
  br i1 %greater.equal, label %if.body, label %if.end

if.end:                                           ; preds = %error.continue
  call void @"std.collections.HashMap<int32,int32>.ensureRoomForNewEntry"(ptr %this)
  %error.pending9 = call i1 @absolute_error_pending()
  br i1 %error.pending9, label %error.propagate10, label %error.continue11

if.body:                                          ; preds = %error.continue
  %values.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value = load %absolute.array.int32.1, ptr %values.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %values.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %values.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %values.value, 2
  %values.address3 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value4 = load %absolute.array.int32.1, ptr %values.address3, align 8
  %array.data5 = extractvalue %absolute.array.int32.1 %values.value4, 0
  %array.owner6 = extractvalue %absolute.array.int32.1 %values.value4, 1
  %array.dimension7 = extractvalue %absolute.array.int32.1 %values.value4, 2
  %existing.value8 = load i32, ptr %existing, align 4
  %array.index.wide = sext i32 %existing.value8 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension7
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

array.bounds.success:                             ; preds = %if.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data5, i32 %existing.value8
  %move.value = load i32, ptr %value2, align 4
  call void @llvm.memset.p0.i64(ptr align 8 %value2, i8 0, i64 4, i1 false)
  store i32 %move.value, ptr %array.element.address, align 4
  ret void

array.bounds.failure:                             ; preds = %if.body
  %0 = call i32 @puts(ptr @array.bounds.message.18)
  call void @exit(i32 1)
  unreachable

error.propagate10:                                ; preds = %if.end
  ret void

error.continue11:                                 ; preds = %if.end
  %key.value12 = load i32, ptr %key1, align 4
  %findInsertionIndex.result = call i32 @"std.collections.HashMap<int32,int32>.findInsertionIndex$int32"(ptr %this, i32 %key.value12)
  %error.pending13 = call i1 @absolute_error_pending()
  br i1 %error.pending13, label %error.propagate14, label %error.continue15

error.propagate14:                                ; preds = %error.continue11
  ret void

error.continue15:                                 ; preds = %error.continue11
  store i32 %findInsertionIndex.result, ptr %index, align 4
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value = load %absolute.array.int32.1, ptr %states.address, align 8
  %array.data17 = extractvalue %absolute.array.int32.1 %states.value, 0
  %array.owner18 = extractvalue %absolute.array.int32.1 %states.value, 1
  %array.dimension19 = extractvalue %absolute.array.int32.1 %states.value, 2
  %states.address20 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value21 = load %absolute.array.int32.1, ptr %states.address20, align 8
  %array.data22 = extractvalue %absolute.array.int32.1 %states.value21, 0
  %array.owner23 = extractvalue %absolute.array.int32.1 %states.value21, 1
  %array.dimension24 = extractvalue %absolute.array.int32.1 %states.value21, 2
  %index.value = load i32, ptr %index, align 4
  %array.index.wide25 = sext i32 %index.value to i64
  %array.index.valid26 = icmp ult i64 %array.index.wide25, %array.dimension24
  br i1 %array.index.valid26, label %array.bounds.success27, label %array.bounds.failure28, !prof !0

if.end16:                                         ; preds = %if.body30, %array.bounds.success27
  %keys.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %keys.value = load %absolute.array.int32.1, ptr %keys.address, align 8
  %array.data32 = extractvalue %absolute.array.int32.1 %keys.value, 0
  %array.owner33 = extractvalue %absolute.array.int32.1 %keys.value, 1
  %array.dimension34 = extractvalue %absolute.array.int32.1 %keys.value, 2
  %keys.address35 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %keys.value36 = load %absolute.array.int32.1, ptr %keys.address35, align 8
  %array.data37 = extractvalue %absolute.array.int32.1 %keys.value36, 0
  %array.owner38 = extractvalue %absolute.array.int32.1 %keys.value36, 1
  %array.dimension39 = extractvalue %absolute.array.int32.1 %keys.value36, 2
  %index.value40 = load i32, ptr %index, align 4
  %array.index.wide41 = sext i32 %index.value40 to i64
  %array.index.valid42 = icmp ult i64 %array.index.wide41, %array.dimension39
  br i1 %array.index.valid42, label %array.bounds.success43, label %array.bounds.failure44, !prof !0

array.bounds.success27:                           ; preds = %error.continue15
  %array.element.address29 = getelementptr inbounds i32, ptr %array.data22, i32 %index.value
  %array.element = load i32, ptr %array.element.address29, align 4
  %equal = icmp eq i32 %array.element, 2
  br i1 %equal, label %if.body30, label %if.end16

array.bounds.failure28:                           ; preds = %error.continue15
  %1 = call i32 @puts(ptr @array.bounds.message.19)
  call void @exit(i32 1)
  unreachable

if.body30:                                        ; preds = %array.bounds.success27
  %tombstoneCount.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 5
  %tombstoneCount.address31 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 5
  %tombstoneCount.value = load i32, ptr %tombstoneCount.address31, align 4
  %sub = sub i32 %tombstoneCount.value, 1
  store i32 %sub, ptr %tombstoneCount.address, align 4
  br label %if.end16

array.bounds.success43:                           ; preds = %if.end16
  %array.element.address45 = getelementptr inbounds i32, ptr %array.data37, i32 %index.value40
  %move.value46 = load i32, ptr %key1, align 4
  call void @llvm.memset.p0.i64(ptr align 8 %key1, i8 0, i64 4, i1 false)
  store i32 %move.value46, ptr %array.element.address45, align 4
  %values.address47 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value48 = load %absolute.array.int32.1, ptr %values.address47, align 8
  %array.data49 = extractvalue %absolute.array.int32.1 %values.value48, 0
  %array.owner50 = extractvalue %absolute.array.int32.1 %values.value48, 1
  %array.dimension51 = extractvalue %absolute.array.int32.1 %values.value48, 2
  %values.address52 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value53 = load %absolute.array.int32.1, ptr %values.address52, align 8
  %array.data54 = extractvalue %absolute.array.int32.1 %values.value53, 0
  %array.owner55 = extractvalue %absolute.array.int32.1 %values.value53, 1
  %array.dimension56 = extractvalue %absolute.array.int32.1 %values.value53, 2
  %index.value57 = load i32, ptr %index, align 4
  %array.index.wide58 = sext i32 %index.value57 to i64
  %array.index.valid59 = icmp ult i64 %array.index.wide58, %array.dimension56
  br i1 %array.index.valid59, label %array.bounds.success60, label %array.bounds.failure61, !prof !0

array.bounds.failure44:                           ; preds = %if.end16
  %2 = call i32 @puts(ptr @array.bounds.message.20)
  call void @exit(i32 1)
  unreachable

array.bounds.success60:                           ; preds = %array.bounds.success43
  %array.element.address62 = getelementptr inbounds i32, ptr %array.data54, i32 %index.value57
  %move.value63 = load i32, ptr %value2, align 4
  call void @llvm.memset.p0.i64(ptr align 8 %value2, i8 0, i64 4, i1 false)
  store i32 %move.value63, ptr %array.element.address62, align 4
  %states.address64 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value65 = load %absolute.array.int32.1, ptr %states.address64, align 8
  %array.data66 = extractvalue %absolute.array.int32.1 %states.value65, 0
  %array.owner67 = extractvalue %absolute.array.int32.1 %states.value65, 1
  %array.dimension68 = extractvalue %absolute.array.int32.1 %states.value65, 2
  %states.address69 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value70 = load %absolute.array.int32.1, ptr %states.address69, align 8
  %array.data71 = extractvalue %absolute.array.int32.1 %states.value70, 0
  %array.owner72 = extractvalue %absolute.array.int32.1 %states.value70, 1
  %array.dimension73 = extractvalue %absolute.array.int32.1 %states.value70, 2
  %index.value74 = load i32, ptr %index, align 4
  %array.index.wide75 = sext i32 %index.value74 to i64
  %array.index.valid76 = icmp ult i64 %array.index.wide75, %array.dimension73
  br i1 %array.index.valid76, label %array.bounds.success77, label %array.bounds.failure78, !prof !0

array.bounds.failure61:                           ; preds = %array.bounds.success43
  %3 = call i32 @puts(ptr @array.bounds.message.21)
  call void @exit(i32 1)
  unreachable

array.bounds.success77:                           ; preds = %array.bounds.success60
  %array.element.address79 = getelementptr inbounds i32, ptr %array.data71, i32 %index.value74
  store i32 1, ptr %array.element.address79, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  %_count.address80 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  %_count.value = load i32, ptr %_count.address80, align 4
  %add = add i32 %_count.value, 1
  store i32 %add, ptr %_count.address, align 4
  ret void

array.bounds.failure78:                           ; preds = %array.bounds.success60
  %4 = call i32 @puts(ptr @array.bounds.message.22)
  call void @exit(i32 1)
  unreachable
}

define i32 @"std.collections.HashMap<int32,int32>.__absolute_property_get_capacity"(ptr %this) {
entry:
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  ret i32 %_capacity.value
}

define i1 @"std.collections.HashMap<int32,int32>.__absolute_property_get_isEmpty"(ptr %this) {
entry:
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  %_count.value = load i32, ptr %_count.address, align 4
  %equal = icmp eq i32 %_count.value, 0
  ret i1 %equal
}

define i1 @"std.collections.HashMap<int32,int32>.containsKey$int32"(ptr %this, i32 %key) {
entry:
  %key1 = alloca i32, align 4
  store i32 %key, ptr %key1, align 4
  %key.value = load i32, ptr %key1, align 4
  %findIndex.result = call i32 @"std.collections.HashMap<int32,int32>.findIndex$int32"(ptr %this, i32 %key.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  ret i1 false

error.continue:                                   ; preds = %entry
  %greater.equal = icmp sge i32 %findIndex.result, 0
  ret i1 %greater.equal
}

define i1 @"std.collections.HashMap<int32,int32>.tryAdd$int32$int32"(ptr %this, i32 %key, i32 %value) {
entry:
  %value2 = alloca i32, align 4
  %key1 = alloca i32, align 4
  store i32 %key, ptr %key1, align 4
  store i32 %value, ptr %value2, align 4
  %key.value = load i32, ptr %key1, align 4
  %containsKey.result = call i1 @"std.collections.HashMap<int32,int32>.containsKey$int32"(ptr %this, i32 %key.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

if.end:                                           ; preds = %error.continue
  %key.value3 = load i32, ptr %key1, align 4
  %value.value = load i32, ptr %value2, align 4
  call void @"std.collections.HashMap<int32,int32>.put$int32$int32"(ptr %this, i32 %key.value3, i32 %value.value)
  %error.pending4 = call i1 @absolute_error_pending()
  br i1 %error.pending4, label %error.propagate5, label %error.continue6

error.propagate:                                  ; preds = %entry
  ret i1 false

error.continue:                                   ; preds = %entry
  br i1 %containsKey.result, label %if.body, label %if.end

if.body:                                          ; preds = %error.continue
  ret i1 false

error.propagate5:                                 ; preds = %if.end
  ret i1 false

error.continue6:                                  ; preds = %if.end
  ret i1 true
}

define i64 @"std.collections.HashMap<int32,int32>.iterate"(ptr %this) {
entry:
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 4662798274618787219)
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
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %"absolute.class.std.collections.HashMapIterator<int32,int32>", ptr %managed.final.ptr, i32 0, i32 0
  store ptr @"std.collections.HashMapIterator<int32,int32>.__vtable", ptr %vtable.address, align 8
  %toArray.result = call %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" @"std.collections.HashMap<int32,int32>.toArray"(ptr %this)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  ret i64 0

error.continue:                                   ; preds = %managed.end
  %array.data = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %toArray.result, 0
  %array.owner = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %toArray.result, 1
  %array.dimension = extractvalue %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %toArray.result, 2
  call void @"std.collections.HashMapIterator<int32,int32>.__ctor$std_2Ecollections_2EHashKeyValuePair_3Cint32_2Cint32_3E_5B_5D"(ptr %managed.final.ptr, %"absolute.array.std.collections.HashKeyValuePair<int32,int32>.1" %toArray.result)
  %error.pending1 = call i1 @absolute_error_pending()
  br i1 %error.pending1, label %error.propagate2, label %error.continue3

error.propagate2:                                 ; preds = %error.continue
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i64 0

error.continue3:                                  ; preds = %error.continue
  ret i64 %managed.handle
}

define i1 @"std.collections.HashMap<int32,int32>.remove$int32"(ptr %this, i32 %key) {
entry:
  %index = alloca i32, align 4
  %key1 = alloca i32, align 4
  store i32 %key, ptr %key1, align 4
  %key.value = load i32, ptr %key1, align 4
  %findIndex.result = call i32 @"std.collections.HashMap<int32,int32>.findIndex$int32"(ptr %this, i32 %key.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  ret i1 false

error.continue:                                   ; preds = %entry
  store i32 %findIndex.result, ptr %index, align 4
  %index.value = load i32, ptr %index, align 4
  %less = icmp slt i32 %index.value, 0
  br i1 %less, label %if.body, label %if.end

if.end:                                           ; preds = %error.continue
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value = load %absolute.array.int32.1, ptr %states.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %states.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %states.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %states.value, 2
  %states.address2 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value3 = load %absolute.array.int32.1, ptr %states.address2, align 8
  %array.data4 = extractvalue %absolute.array.int32.1 %states.value3, 0
  %array.owner5 = extractvalue %absolute.array.int32.1 %states.value3, 1
  %array.dimension6 = extractvalue %absolute.array.int32.1 %states.value3, 2
  %index.value7 = load i32, ptr %index, align 4
  %array.index.wide = sext i32 %index.value7 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension6
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

if.body:                                          ; preds = %error.continue
  ret i1 false

array.bounds.success:                             ; preds = %if.end
  %array.element.address = getelementptr inbounds i32, ptr %array.data4, i32 %index.value7
  store i32 2, ptr %array.element.address, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  %_count.address8 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  %_count.value = load i32, ptr %_count.address8, align 4
  %sub = sub i32 %_count.value, 1
  store i32 %sub, ptr %_count.address, align 4
  %tombstoneCount.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 5
  %tombstoneCount.address9 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 5
  %tombstoneCount.value = load i32, ptr %tombstoneCount.address9, align 4
  %add = add i32 %tombstoneCount.value, 1
  store i32 %add, ptr %tombstoneCount.address, align 4
  ret i1 true

array.bounds.failure:                             ; preds = %if.end
  %0 = call i32 @puts(ptr @array.bounds.message.23)
  call void @exit(i32 1)
  unreachable
}

define i32 @"std.collections.HashMap<int32,int32>.__absolute_indexer_get$int32"(ptr %this, i32 %key) {
entry:
  %index = alloca i32, align 4
  %key1 = alloca i32, align 4
  store i32 %key, ptr %key1, align 4
  %key.value = load i32, ptr %key1, align 4
  %findIndex.result = call i32 @"std.collections.HashMap<int32,int32>.findIndex$int32"(ptr %this, i32 %key.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  ret i32 0

error.continue:                                   ; preds = %entry
  store i32 %findIndex.result, ptr %index, align 4
  %index.value = load i32, ptr %index, align 4
  %less = icmp slt i32 %index.value, 0
  br i1 %less, label %if.body, label %if.end

if.end:                                           ; preds = %error.continue
  %values.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value = load %absolute.array.int32.1, ptr %values.address, align 8
  %array.data = extractvalue %absolute.array.int32.1 %values.value, 0
  %array.owner = extractvalue %absolute.array.int32.1 %values.value, 1
  %array.dimension = extractvalue %absolute.array.int32.1 %values.value, 2
  %values.address5 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value6 = load %absolute.array.int32.1, ptr %values.address5, align 8
  %array.data7 = extractvalue %absolute.array.int32.1 %values.value6, 0
  %array.owner8 = extractvalue %absolute.array.int32.1 %values.value6, 1
  %array.dimension9 = extractvalue %absolute.array.int32.1 %values.value6, 2
  %index.value10 = load i32, ptr %index, align 4
  %array.index.wide = sext i32 %index.value10 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension9
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

if.body:                                          ; preds = %error.continue
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
  %error.pending2 = call i1 @absolute_error_pending()
  br i1 %error.pending2, label %error.propagate3, label %error.continue4

error.propagate3:                                 ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  ret i32 0

error.continue4:                                  ; preds = %managed.end
  %exception.dynamic.type = call i64 @absolute_managed_type(i64 %managed.handle)
  %1 = icmp ne i64 %exception.dynamic.type, 0
  %exception.effective.type = select i1 %1, i64 %exception.dynamic.type, i64 6860172195720241041
  call void @absolute_error_set(i64 %managed.handle, i64 %exception.effective.type)
  ret i32 0

array.bounds.success:                             ; preds = %if.end
  %array.element.address = getelementptr inbounds i32, ptr %array.data7, i32 %index.value10
  %array.element = load i32, ptr %array.element.address, align 4
  ret i32 %array.element

array.bounds.failure:                             ; preds = %if.end
  %2 = call i32 @puts(ptr @array.bounds.message.24)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.HashMap<int32,int32>.__absolute_indexer_set$int32$int32"(ptr %this, i32 %key, i32 %value) {
entry:
  %value2 = alloca i32, align 4
  %key1 = alloca i32, align 4
  store i32 %key, ptr %key1, align 4
  store i32 %value, ptr %value2, align 4
  %key.value = load i32, ptr %key1, align 4
  %value.value = load i32, ptr %value2, align 4
  call void @"std.collections.HashMap<int32,int32>.put$int32$int32"(ptr %this, i32 %key.value, i32 %value.value)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  ret void

error.continue:                                   ; preds = %entry
  ret void
}

define void @"std.collections.HashMap<int32,int32>.clear"(ptr %this) {
entry:
  %keys.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %int.cast = sext i32 %_capacity.value to i64
  %array.alloc.bytes = mul i64 %int.cast, 4
  %array.data.alloc = call ptr @malloc(i64 %array.alloc.bytes)
  %array.data = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc, 0
  %array.owner = insertvalue %absolute.array.int32.1 %array.data, ptr %array.data.alloc, 1
  %array.dimension = insertvalue %absolute.array.int32.1 %array.owner, i64 %int.cast, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %keys.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %keys.address, align 8
  store %absolute.array.int32.1 %array.dimension, ptr %keys.address, align 8
  %values.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %_capacity.address1 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value2 = load i32, ptr %_capacity.address1, align 4
  %int.cast3 = sext i32 %_capacity.value2 to i64
  %array.alloc.bytes4 = mul i64 %int.cast3, 4
  %array.data.alloc5 = call ptr @malloc(i64 %array.alloc.bytes4)
  %array.data6 = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc5, 0
  %array.owner7 = insertvalue %absolute.array.int32.1 %array.data6, ptr %array.data.alloc5, 1
  %array.dimension8 = insertvalue %absolute.array.int32.1 %array.owner7, i64 %int.cast3, 2
  %field.cleanup.array9 = load %absolute.array.int32.1, ptr %values.address, align 8
  %field.cleanup.array.owner10 = extractvalue %absolute.array.int32.1 %field.cleanup.array9, 1
  call void @free(ptr %field.cleanup.array.owner10)
  store %absolute.array.int32.1 zeroinitializer, ptr %values.address, align 8
  store %absolute.array.int32.1 %array.dimension8, ptr %values.address, align 8
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %_capacity.address11 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value12 = load i32, ptr %_capacity.address11, align 4
  %int.cast13 = sext i32 %_capacity.value12 to i64
  %array.alloc.bytes14 = mul i64 %int.cast13, 4
  %array.data.alloc15 = call ptr @malloc(i64 %array.alloc.bytes14)
  %array.data16 = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc15, 0
  %array.owner17 = insertvalue %absolute.array.int32.1 %array.data16, ptr %array.data.alloc15, 1
  %array.dimension18 = insertvalue %absolute.array.int32.1 %array.owner17, i64 %int.cast13, 2
  %field.cleanup.array19 = load %absolute.array.int32.1, ptr %states.address, align 8
  %field.cleanup.array.owner20 = extractvalue %absolute.array.int32.1 %field.cleanup.array19, 1
  call void @free(ptr %field.cleanup.array.owner20)
  store %absolute.array.int32.1 zeroinitializer, ptr %states.address, align 8
  store %absolute.array.int32.1 %array.dimension18, ptr %states.address, align 8
  call void @"std.collections.HashMap<int32,int32>.clearStates"(ptr %this)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %entry
  ret void

error.continue:                                   ; preds = %entry
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  store i32 0, ptr %_count.address, align 4
  %tombstoneCount.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 5
  store i32 0, ptr %tombstoneCount.address, align 4
  ret void
}

define %absolute.array.int32.1 @"std.collections.HashMap<int32,int32>.keysToArray"(ptr %this) {
entry:
  %resultIndex = alloca i32, align 4
  %sourceIndex = alloca i32, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
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
  store i32 0, ptr %sourceIndex, align 4
  store i32 0, ptr %resultIndex, align 4
  br label %while.condition

while.condition:                                  ; preds = %if.end, %entry
  %sourceIndex.value = load i32, ptr %sourceIndex, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %less = icmp slt i32 %sourceIndex.value, %_capacity.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value = load %absolute.array.int32.1, ptr %states.address, align 8
  %array.data7 = extractvalue %absolute.array.int32.1 %states.value, 0
  %array.owner8 = extractvalue %absolute.array.int32.1 %states.value, 1
  %array.dimension9 = extractvalue %absolute.array.int32.1 %states.value, 2
  %states.address10 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value11 = load %absolute.array.int32.1, ptr %states.address10, align 8
  %array.data12 = extractvalue %absolute.array.int32.1 %states.value11, 0
  %array.owner13 = extractvalue %absolute.array.int32.1 %states.value11, 1
  %array.dimension14 = extractvalue %absolute.array.int32.1 %states.value11, 2
  %sourceIndex.value15 = load i32, ptr %sourceIndex, align 4
  %array.index.wide = sext i32 %sourceIndex.value15 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension14
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %array.data39 = insertvalue %absolute.array.int32.1 undef, ptr %array.data1, 0
  %array.owner40 = insertvalue %absolute.array.int32.1 %array.data39, ptr %array.owner2, 1
  %array.dimension41 = insertvalue %absolute.array.int32.1 %array.owner40, i64 %array.dimension3, 2
  ret %absolute.array.int32.1 %array.dimension41

if.end:                                           ; preds = %array.bounds.success32, %array.bounds.success
  %sourceIndex.value37 = load i32, ptr %sourceIndex, align 4
  %add38 = add i32 %sourceIndex.value37, 1
  store i32 %add38, ptr %sourceIndex, align 4
  br label %while.condition

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data12, i32 %sourceIndex.value15
  %array.element = load i32, ptr %array.element.address, align 4
  %equal = icmp eq i32 %array.element, 1
  br i1 %equal, label %if.body, label %if.end

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.25)
  call void @exit(i32 1)
  unreachable

if.body:                                          ; preds = %array.bounds.success
  %resultIndex.value = load i32, ptr %resultIndex, align 4
  %array.index.wide16 = sext i32 %resultIndex.value to i64
  %array.index.valid17 = icmp ult i64 %array.index.wide16, %array.dimension3
  br i1 %array.index.valid17, label %array.bounds.success18, label %array.bounds.failure19, !prof !0

array.bounds.success18:                           ; preds = %if.body
  %array.element.address20 = getelementptr inbounds i32, ptr %array.data1, i32 %resultIndex.value
  %keys.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %keys.value = load %absolute.array.int32.1, ptr %keys.address, align 8
  %array.data21 = extractvalue %absolute.array.int32.1 %keys.value, 0
  %array.owner22 = extractvalue %absolute.array.int32.1 %keys.value, 1
  %array.dimension23 = extractvalue %absolute.array.int32.1 %keys.value, 2
  %keys.address24 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %keys.value25 = load %absolute.array.int32.1, ptr %keys.address24, align 8
  %array.data26 = extractvalue %absolute.array.int32.1 %keys.value25, 0
  %array.owner27 = extractvalue %absolute.array.int32.1 %keys.value25, 1
  %array.dimension28 = extractvalue %absolute.array.int32.1 %keys.value25, 2
  %sourceIndex.value29 = load i32, ptr %sourceIndex, align 4
  %array.index.wide30 = sext i32 %sourceIndex.value29 to i64
  %array.index.valid31 = icmp ult i64 %array.index.wide30, %array.dimension28
  br i1 %array.index.valid31, label %array.bounds.success32, label %array.bounds.failure33, !prof !0

array.bounds.failure19:                           ; preds = %if.body
  %1 = call i32 @puts(ptr @array.bounds.message.26)
  call void @exit(i32 1)
  unreachable

array.bounds.success32:                           ; preds = %array.bounds.success18
  %array.element.address34 = getelementptr inbounds i32, ptr %array.data26, i32 %sourceIndex.value29
  %array.element35 = load i32, ptr %array.element.address34, align 4
  store i32 %array.element35, ptr %array.element.address20, align 4
  %resultIndex.value36 = load i32, ptr %resultIndex, align 4
  %add = add i32 %resultIndex.value36, 1
  store i32 %add, ptr %resultIndex, align 4
  br label %if.end

array.bounds.failure33:                           ; preds = %array.bounds.success18
  %2 = call i32 @puts(ptr @array.bounds.message.27)
  call void @exit(i32 1)
  unreachable
}

define %absolute.array.int32.1 @"std.collections.HashMap<int32,int32>.valuesToArray"(ptr %this) {
entry:
  %resultIndex = alloca i32, align 4
  %sourceIndex = alloca i32, align 4
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
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
  store i32 0, ptr %sourceIndex, align 4
  store i32 0, ptr %resultIndex, align 4
  br label %while.condition

while.condition:                                  ; preds = %if.end, %entry
  %sourceIndex.value = load i32, ptr %sourceIndex, align 4
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address, align 4
  %less = icmp slt i32 %sourceIndex.value, %_capacity.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value = load %absolute.array.int32.1, ptr %states.address, align 8
  %array.data7 = extractvalue %absolute.array.int32.1 %states.value, 0
  %array.owner8 = extractvalue %absolute.array.int32.1 %states.value, 1
  %array.dimension9 = extractvalue %absolute.array.int32.1 %states.value, 2
  %states.address10 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value11 = load %absolute.array.int32.1, ptr %states.address10, align 8
  %array.data12 = extractvalue %absolute.array.int32.1 %states.value11, 0
  %array.owner13 = extractvalue %absolute.array.int32.1 %states.value11, 1
  %array.dimension14 = extractvalue %absolute.array.int32.1 %states.value11, 2
  %sourceIndex.value15 = load i32, ptr %sourceIndex, align 4
  %array.index.wide = sext i32 %sourceIndex.value15 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension14
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %array.data39 = insertvalue %absolute.array.int32.1 undef, ptr %array.data1, 0
  %array.owner40 = insertvalue %absolute.array.int32.1 %array.data39, ptr %array.owner2, 1
  %array.dimension41 = insertvalue %absolute.array.int32.1 %array.owner40, i64 %array.dimension3, 2
  ret %absolute.array.int32.1 %array.dimension41

if.end:                                           ; preds = %array.bounds.success32, %array.bounds.success
  %sourceIndex.value37 = load i32, ptr %sourceIndex, align 4
  %add38 = add i32 %sourceIndex.value37, 1
  store i32 %add38, ptr %sourceIndex, align 4
  br label %while.condition

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data12, i32 %sourceIndex.value15
  %array.element = load i32, ptr %array.element.address, align 4
  %equal = icmp eq i32 %array.element, 1
  br i1 %equal, label %if.body, label %if.end

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.28)
  call void @exit(i32 1)
  unreachable

if.body:                                          ; preds = %array.bounds.success
  %resultIndex.value = load i32, ptr %resultIndex, align 4
  %array.index.wide16 = sext i32 %resultIndex.value to i64
  %array.index.valid17 = icmp ult i64 %array.index.wide16, %array.dimension3
  br i1 %array.index.valid17, label %array.bounds.success18, label %array.bounds.failure19, !prof !0

array.bounds.success18:                           ; preds = %if.body
  %array.element.address20 = getelementptr inbounds i32, ptr %array.data1, i32 %resultIndex.value
  %values.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value = load %absolute.array.int32.1, ptr %values.address, align 8
  %array.data21 = extractvalue %absolute.array.int32.1 %values.value, 0
  %array.owner22 = extractvalue %absolute.array.int32.1 %values.value, 1
  %array.dimension23 = extractvalue %absolute.array.int32.1 %values.value, 2
  %values.address24 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %values.value25 = load %absolute.array.int32.1, ptr %values.address24, align 8
  %array.data26 = extractvalue %absolute.array.int32.1 %values.value25, 0
  %array.owner27 = extractvalue %absolute.array.int32.1 %values.value25, 1
  %array.dimension28 = extractvalue %absolute.array.int32.1 %values.value25, 2
  %sourceIndex.value29 = load i32, ptr %sourceIndex, align 4
  %array.index.wide30 = sext i32 %sourceIndex.value29 to i64
  %array.index.valid31 = icmp ult i64 %array.index.wide30, %array.dimension28
  br i1 %array.index.valid31, label %array.bounds.success32, label %array.bounds.failure33, !prof !0

array.bounds.failure19:                           ; preds = %if.body
  %1 = call i32 @puts(ptr @array.bounds.message.29)
  call void @exit(i32 1)
  unreachable

array.bounds.success32:                           ; preds = %array.bounds.success18
  %array.element.address34 = getelementptr inbounds i32, ptr %array.data26, i32 %sourceIndex.value29
  %array.element35 = load i32, ptr %array.element.address34, align 4
  store i32 %array.element35, ptr %array.element.address20, align 4
  %resultIndex.value36 = load i32, ptr %resultIndex, align 4
  %add = add i32 %resultIndex.value36, 1
  store i32 %add, ptr %resultIndex, align 4
  br label %if.end

array.bounds.failure33:                           ; preds = %array.bounds.success18
  %2 = call i32 @puts(ptr @array.bounds.message.30)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.HashMap<int32,int32>.__ctor$func_3Cint64_2Cint32_3E$func_3Cbool_2Cint32_2Cint32_3E"(ptr %this, ptr %hash, ptr %equals) {
entry:
  %index = alloca i32, align 4
  %equals2 = alloca ptr, align 8
  %hash1 = alloca ptr, align 8
  store ptr %hash, ptr %hash1, align 8
  store ptr %equals, ptr %equals2, align 8
  %hashFunction.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 7
  %hash.value = load ptr, ptr %hash1, align 8
  call void @__absolute.closure.retain(ptr %hash.value)
  %closure.cleanup.value = load ptr, ptr %hashFunction.address, align 8
  call void @__absolute.closure.release(ptr %closure.cleanup.value)
  store ptr null, ptr %hashFunction.address, align 8
  store ptr %hash.value, ptr %hashFunction.address, align 8
  %equalityFunction.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 8
  %equals.value = load ptr, ptr %equals2, align 8
  call void @__absolute.closure.retain(ptr %equals.value)
  %closure.cleanup.value3 = load ptr, ptr %equalityFunction.address, align 8
  call void @__absolute.closure.release(ptr %closure.cleanup.value3)
  store ptr null, ptr %equalityFunction.address, align 8
  store ptr %equals.value, ptr %equalityFunction.address, align 8
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  store i32 8, ptr %_capacity.address, align 4
  %keys.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %_capacity.address4 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address4, align 4
  %int.cast = sext i32 %_capacity.value to i64
  %array.alloc.bytes = mul i64 %int.cast, 4
  %array.data.alloc = call ptr @malloc(i64 %array.alloc.bytes)
  %array.data = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc, 0
  %array.owner = insertvalue %absolute.array.int32.1 %array.data, ptr %array.data.alloc, 1
  %array.dimension = insertvalue %absolute.array.int32.1 %array.owner, i64 %int.cast, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %keys.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %keys.address, align 8
  store %absolute.array.int32.1 %array.dimension, ptr %keys.address, align 8
  %values.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %_capacity.address5 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value6 = load i32, ptr %_capacity.address5, align 4
  %int.cast7 = sext i32 %_capacity.value6 to i64
  %array.alloc.bytes8 = mul i64 %int.cast7, 4
  %array.data.alloc9 = call ptr @malloc(i64 %array.alloc.bytes8)
  %array.data10 = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc9, 0
  %array.owner11 = insertvalue %absolute.array.int32.1 %array.data10, ptr %array.data.alloc9, 1
  %array.dimension12 = insertvalue %absolute.array.int32.1 %array.owner11, i64 %int.cast7, 2
  %field.cleanup.array13 = load %absolute.array.int32.1, ptr %values.address, align 8
  %field.cleanup.array.owner14 = extractvalue %absolute.array.int32.1 %field.cleanup.array13, 1
  call void @free(ptr %field.cleanup.array.owner14)
  store %absolute.array.int32.1 zeroinitializer, ptr %values.address, align 8
  store %absolute.array.int32.1 %array.dimension12, ptr %values.address, align 8
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %_capacity.address15 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value16 = load i32, ptr %_capacity.address15, align 4
  %int.cast17 = sext i32 %_capacity.value16 to i64
  %array.alloc.bytes18 = mul i64 %int.cast17, 4
  %array.data.alloc19 = call ptr @malloc(i64 %array.alloc.bytes18)
  %array.data20 = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc19, 0
  %array.owner21 = insertvalue %absolute.array.int32.1 %array.data20, ptr %array.data.alloc19, 1
  %array.dimension22 = insertvalue %absolute.array.int32.1 %array.owner21, i64 %int.cast17, 2
  %field.cleanup.array23 = load %absolute.array.int32.1, ptr %states.address, align 8
  %field.cleanup.array.owner24 = extractvalue %absolute.array.int32.1 %field.cleanup.array23, 1
  call void @free(ptr %field.cleanup.array.owner24)
  store %absolute.array.int32.1 zeroinitializer, ptr %states.address, align 8
  store %absolute.array.int32.1 %array.dimension22, ptr %states.address, align 8
  store i32 0, ptr %index, align 4
  br label %while.condition

while.condition:                                  ; preds = %array.bounds.success, %entry
  %index.value = load i32, ptr %index, align 4
  %_capacity.address25 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value26 = load i32, ptr %_capacity.address25, align 4
  %less = icmp slt i32 %index.value, %_capacity.value26
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %states.address27 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value = load %absolute.array.int32.1, ptr %states.address27, align 8
  %array.data28 = extractvalue %absolute.array.int32.1 %states.value, 0
  %array.owner29 = extractvalue %absolute.array.int32.1 %states.value, 1
  %array.dimension30 = extractvalue %absolute.array.int32.1 %states.value, 2
  %states.address31 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value32 = load %absolute.array.int32.1, ptr %states.address31, align 8
  %array.data33 = extractvalue %absolute.array.int32.1 %states.value32, 0
  %array.owner34 = extractvalue %absolute.array.int32.1 %states.value32, 1
  %array.dimension35 = extractvalue %absolute.array.int32.1 %states.value32, 2
  %index.value36 = load i32, ptr %index, align 4
  %array.index.wide = sext i32 %index.value36 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension35
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  store i32 0, ptr %_count.address, align 4
  %tombstoneCount.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 5
  store i32 0, ptr %tombstoneCount.address, align 4
  ret void

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data33, i32 %index.value36
  store i32 0, ptr %array.element.address, align 4
  %index.value37 = load i32, ptr %index, align 4
  %add = add i32 %index.value37, 1
  store i32 %add, ptr %index, align 4
  br label %while.condition

array.bounds.failure:                             ; preds = %while.body
  %0 = call i32 @puts(ptr @array.bounds.message.31)
  call void @exit(i32 1)
  unreachable
}

define void @"std.collections.HashMap<int32,int32>.__ctor$func_3Cint64_2Cint32_3E$func_3Cbool_2Cint32_2Cint32_3E$int32"(ptr %this, ptr %hash, ptr %equals, i32 %initialCapacity) {
entry:
  %index = alloca i32, align 4
  %initialCapacity3 = alloca i32, align 4
  %equals2 = alloca ptr, align 8
  %hash1 = alloca ptr, align 8
  store ptr %hash, ptr %hash1, align 8
  store ptr %equals, ptr %equals2, align 8
  store i32 %initialCapacity, ptr %initialCapacity3, align 4
  %initialCapacity.value = load i32, ptr %initialCapacity3, align 4
  %less = icmp slt i32 %initialCapacity.value, 1
  br i1 %less, label %if.body, label %if.end

if.end:                                           ; preds = %entry
  %hashFunction.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 7
  %hash.value = load ptr, ptr %hash1, align 8
  call void @__absolute.closure.retain(ptr %hash.value)
  %closure.cleanup.value = load ptr, ptr %hashFunction.address, align 8
  call void @__absolute.closure.release(ptr %closure.cleanup.value)
  store ptr null, ptr %hashFunction.address, align 8
  store ptr %hash.value, ptr %hashFunction.address, align 8
  %equalityFunction.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 8
  %equals.value = load ptr, ptr %equals2, align 8
  call void @__absolute.closure.retain(ptr %equals.value)
  %closure.cleanup.value4 = load ptr, ptr %equalityFunction.address, align 8
  call void @__absolute.closure.release(ptr %closure.cleanup.value4)
  store ptr null, ptr %equalityFunction.address, align 8
  store ptr %equals.value, ptr %equalityFunction.address, align 8
  %_capacity.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %initialCapacity.value5 = load i32, ptr %initialCapacity3, align 4
  %less6 = icmp slt i32 %initialCapacity.value5, 4
  br i1 %less6, label %ternary.true, label %ternary.false

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

ternary.true:                                     ; preds = %if.end
  br label %ternary.end

ternary.false:                                    ; preds = %if.end
  %initialCapacity.value7 = load i32, ptr %initialCapacity3, align 4
  br label %ternary.end

ternary.end:                                      ; preds = %ternary.false, %ternary.true
  %ternary.result = phi i32 [ 4, %ternary.true ], [ %initialCapacity.value7, %ternary.false ]
  store i32 %ternary.result, ptr %_capacity.address, align 4
  %keys.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %_capacity.address8 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value = load i32, ptr %_capacity.address8, align 4
  %int.cast = sext i32 %_capacity.value to i64
  %array.alloc.bytes = mul i64 %int.cast, 4
  %array.data.alloc = call ptr @malloc(i64 %array.alloc.bytes)
  %array.data = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc, 0
  %array.owner = insertvalue %absolute.array.int32.1 %array.data, ptr %array.data.alloc, 1
  %array.dimension = insertvalue %absolute.array.int32.1 %array.owner, i64 %int.cast, 2
  %field.cleanup.array = load %absolute.array.int32.1, ptr %keys.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %keys.address, align 8
  store %absolute.array.int32.1 %array.dimension, ptr %keys.address, align 8
  %values.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %_capacity.address9 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value10 = load i32, ptr %_capacity.address9, align 4
  %int.cast11 = sext i32 %_capacity.value10 to i64
  %array.alloc.bytes12 = mul i64 %int.cast11, 4
  %array.data.alloc13 = call ptr @malloc(i64 %array.alloc.bytes12)
  %array.data14 = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc13, 0
  %array.owner15 = insertvalue %absolute.array.int32.1 %array.data14, ptr %array.data.alloc13, 1
  %array.dimension16 = insertvalue %absolute.array.int32.1 %array.owner15, i64 %int.cast11, 2
  %field.cleanup.array17 = load %absolute.array.int32.1, ptr %values.address, align 8
  %field.cleanup.array.owner18 = extractvalue %absolute.array.int32.1 %field.cleanup.array17, 1
  call void @free(ptr %field.cleanup.array.owner18)
  store %absolute.array.int32.1 zeroinitializer, ptr %values.address, align 8
  store %absolute.array.int32.1 %array.dimension16, ptr %values.address, align 8
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %_capacity.address19 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value20 = load i32, ptr %_capacity.address19, align 4
  %int.cast21 = sext i32 %_capacity.value20 to i64
  %array.alloc.bytes22 = mul i64 %int.cast21, 4
  %array.data.alloc23 = call ptr @malloc(i64 %array.alloc.bytes22)
  %array.data24 = insertvalue %absolute.array.int32.1 undef, ptr %array.data.alloc23, 0
  %array.owner25 = insertvalue %absolute.array.int32.1 %array.data24, ptr %array.data.alloc23, 1
  %array.dimension26 = insertvalue %absolute.array.int32.1 %array.owner25, i64 %int.cast21, 2
  %field.cleanup.array27 = load %absolute.array.int32.1, ptr %states.address, align 8
  %field.cleanup.array.owner28 = extractvalue %absolute.array.int32.1 %field.cleanup.array27, 1
  call void @free(ptr %field.cleanup.array.owner28)
  store %absolute.array.int32.1 zeroinitializer, ptr %states.address, align 8
  store %absolute.array.int32.1 %array.dimension26, ptr %states.address, align 8
  store i32 0, ptr %index, align 4
  br label %while.condition

while.condition:                                  ; preds = %array.bounds.success, %ternary.end
  %index.value = load i32, ptr %index, align 4
  %_capacity.address29 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 6
  %_capacity.value30 = load i32, ptr %_capacity.address29, align 4
  %less31 = icmp slt i32 %index.value, %_capacity.value30
  br i1 %less31, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %states.address32 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value = load %absolute.array.int32.1, ptr %states.address32, align 8
  %array.data33 = extractvalue %absolute.array.int32.1 %states.value, 0
  %array.owner34 = extractvalue %absolute.array.int32.1 %states.value, 1
  %array.dimension35 = extractvalue %absolute.array.int32.1 %states.value, 2
  %states.address36 = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %states.value37 = load %absolute.array.int32.1, ptr %states.address36, align 8
  %array.data38 = extractvalue %absolute.array.int32.1 %states.value37, 0
  %array.owner39 = extractvalue %absolute.array.int32.1 %states.value37, 1
  %array.dimension40 = extractvalue %absolute.array.int32.1 %states.value37, 2
  %index.value41 = load i32, ptr %index, align 4
  %array.index.wide = sext i32 %index.value41 to i64
  %array.index.valid = icmp ult i64 %array.index.wide, %array.dimension40
  br i1 %array.index.valid, label %array.bounds.success, label %array.bounds.failure, !prof !0

while.end:                                        ; preds = %while.condition
  %_count.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 4
  store i32 0, ptr %_count.address, align 4
  %tombstoneCount.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 5
  store i32 0, ptr %tombstoneCount.address, align 4
  ret void

array.bounds.success:                             ; preds = %while.body
  %array.element.address = getelementptr inbounds i32, ptr %array.data38, i32 %index.value41
  store i32 0, ptr %array.element.address, align 4
  %index.value42 = load i32, ptr %index, align 4
  %add = add i32 %index.value42, 1
  store i32 %add, ptr %index, align 4
  br label %while.condition

array.bounds.failure:                             ; preds = %while.body
  %2 = call i32 @puts(ptr @array.bounds.message.33)
  call void @exit(i32 1)
  unreachable
}

define internal void @"std.collections.HashMap<int32,int32>.__destroy"(ptr %this) {
entry:
  %equalityFunction.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 8
  %closure.cleanup.value = load ptr, ptr %equalityFunction.address, align 8
  call void @__absolute.closure.release(ptr %closure.cleanup.value)
  store ptr null, ptr %equalityFunction.address, align 8
  %hashFunction.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 7
  %closure.cleanup.value1 = load ptr, ptr %hashFunction.address, align 8
  call void @__absolute.closure.release(ptr %closure.cleanup.value1)
  store ptr null, ptr %hashFunction.address, align 8
  %states.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 3
  %field.cleanup.array = load %absolute.array.int32.1, ptr %states.address, align 8
  %field.cleanup.array.owner = extractvalue %absolute.array.int32.1 %field.cleanup.array, 1
  call void @free(ptr %field.cleanup.array.owner)
  store %absolute.array.int32.1 zeroinitializer, ptr %states.address, align 8
  %values.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 2
  %field.cleanup.array2 = load %absolute.array.int32.1, ptr %values.address, align 8
  %field.cleanup.array.owner3 = extractvalue %absolute.array.int32.1 %field.cleanup.array2, 1
  call void @free(ptr %field.cleanup.array.owner3)
  store %absolute.array.int32.1 zeroinitializer, ptr %values.address, align 8
  %keys.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %this, i32 0, i32 1
  %field.cleanup.array4 = load %absolute.array.int32.1, ptr %keys.address, align 8
  %field.cleanup.array.owner5 = extractvalue %absolute.array.int32.1 %field.cleanup.array4, 1
  call void @free(ptr %field.cleanup.array.owner5)
  store %absolute.array.int32.1 zeroinitializer, ptr %keys.address, align 8
  ret void
}

define i64 @hashInt32(i32 %value) {
entry:
  %h = alloca i32, align 4
  %value1 = alloca i32, align 4
  store i32 %value, ptr %value1, align 4
  %value.value = load i32, ptr %value1, align 4
  %mul = mul i32 %value.value, -1640531535
  store i32 %mul, ptr %h, align 4
  %h.value = load i32, ptr %h, align 4
  %int.cast = zext i32 %h.value to i64
  ret i64 %int.cast
}

define i1 @equalsInt32(i32 %left, i32 %right) {
entry:
  %right2 = alloca i32, align 4
  %left1 = alloca i32, align 4
  store i32 %left, ptr %left1, align 4
  store i32 %right, ptr %right2, align 4
  %left.value = load i32, ptr %left1, align 4
  %right.value = load i32, ptr %right2, align 4
  %equal = icmp eq i32 %left.value, %right.value
  ret i1 %equal
}

define i32 @main() {
entry:
  %k61 = alloca i32, align 4
  %checksum = alloca i64, align 8
  %v = alloca i32, align 4
  %k = alloca i32, align 4
  %i = alloca i32, align 4
  %map.cached.pointee = alloca ptr, align 8
  %map = alloca i64, align 8
  %N = alloca i32, align 4
  store i32 200000, ptr %N, align 4
  %managed.handle = call i64 @absolute_managed_create(i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.HashMap<int32,int32>", ptr null, i32 1) to i64))
  call void @absolute_managed_set_type(i64 %managed.handle, i64 -515207750487347487)
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
  call void @llvm.memset.p0.i64(ptr align 8 %managed.final.ptr, i8 0, i64 ptrtoint (ptr getelementptr (%"absolute.class.std.collections.HashMap<int32,int32>", ptr null, i32 1) to i64), i1 false)
  %vtable.address = getelementptr inbounds %"absolute.class.std.collections.HashMap<int32,int32>", ptr %managed.final.ptr, i32 0, i32 0
  store ptr @"std.collections.HashMap<int32,int32>.__vtable", ptr %vtable.address, align 8
  call void @"std.collections.HashMap<int32,int32>.__ctor$func_3Cint64_2Cint32_3E$func_3Cbool_2Cint32_2Cint32_3E"(ptr %managed.final.ptr, ptr @__absolute.closure.value.hashInt32, ptr @__absolute.closure.value.equalsInt32)
  %error.pending = call i1 @absolute_error_pending()
  br i1 %error.pending, label %error.propagate, label %error.continue

error.propagate:                                  ; preds = %managed.end
  call void @absolute_managed_destroy(i64 %managed.handle)
  %cleanup.handle = load i64, ptr %map, align 4
  %managed.id6 = trunc i64 %cleanup.handle to i32
  %1 = lshr i64 %cleanup.handle, 32
  %managed.gen7 = trunc i64 %1 to i32
  %managed.slots.size8 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds9 = icmp ult i32 %managed.id6, %managed.slots.size8
  br i1 %managed.in.bounds9, label %managed.fast.load2, label %managed.slow4

error.continue:                                   ; preds = %managed.end
  store i64 %managed.handle, ptr %map, align 4
  store ptr %managed.final.ptr, ptr %map.cached.pointee, align 8
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
  store i64 0, ptr %map, align 4
  call void @absolute_error_report()
  ret i32 1

while.condition:                                  ; preds = %error.continue27, %error.continue
  %i.value = load i32, ptr %i, align 4
  %N.value = load i32, ptr %N, align 4
  %less = icmp slt i32 %i.value, %N.value
  br i1 %less, label %while.body, label %while.end

while.body:                                       ; preds = %while.condition
  %i.value20 = load i32, ptr %i, align 4
  %mul = mul i32 %i.value20, 17
  %add = add i32 %mul, 31
  store i32 %add, ptr %k, align 4
  %i.value21 = load i32, ptr %i, align 4
  %mul22 = mul i32 %i.value21, 13
  %add23 = add i32 %mul22, 7
  store i32 %add23, ptr %v, align 4
  %map.value = load i64, ptr %map, align 4
  %map.cached.pointee24 = load ptr, ptr %map.cached.pointee, align 8
  %k.value = load i32, ptr %k, align 4
  %v.value = load i32, ptr %v, align 4
  call void @"std.collections.HashMap<int32,int32>.put$int32$int32"(ptr %map.cached.pointee24, i32 %k.value, i32 %v.value)
  %error.pending25 = call i1 @absolute_error_pending()
  br i1 %error.pending25, label %error.propagate26, label %error.continue27

while.end:                                        ; preds = %while.condition
  store i64 0, ptr %checksum, align 4
  store i32 0, ptr %i, align 4
  br label %while.condition55

error.propagate26:                                ; preds = %while.body
  %cleanup.handle28 = load i64, ptr %map, align 4
  %managed.id34 = trunc i64 %cleanup.handle28 to i32
  %2 = lshr i64 %cleanup.handle28, 32
  %managed.gen35 = trunc i64 %2 to i32
  %managed.slots.size36 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds37 = icmp ult i32 %managed.id34, %managed.slots.size36
  br i1 %managed.in.bounds37, label %managed.fast.load30, label %managed.slow32

error.continue27:                                 ; preds = %while.body
  %assignment.current = load i32, ptr %i, align 4
  %add54 = add i32 %assignment.current, 1
  store i32 %add54, ptr %i, align 4
  br label %while.condition

managed.fast.check29:                             ; preds = %managed.fast.load30
  %managed.slot.ptr.field43 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr39, i32 0, i32 0
  %managed.ptr44 = load ptr, ptr %managed.slot.ptr.field43, align 8
  %managed.ptr.not.null45 = icmp ne ptr %managed.ptr44, null
  br i1 %managed.ptr.not.null45, label %managed.fast.valid31, label %managed.slow32

managed.fast.load30:                              ; preds = %error.propagate26
  %managed.slots.data38 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr39 = getelementptr %absolute.Slot, ptr %managed.slots.data38, i32 %managed.id34
  %managed.slot.gen.ptr40 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr39, i32 0, i32 1
  %managed.slot.gen41 = load i32, ptr %managed.slot.gen.ptr40, align 4
  %managed.gen.match42 = icmp eq i32 %managed.slot.gen41, %managed.gen35
  br i1 %managed.gen.match42, label %managed.fast.check29, label %managed.slow32

managed.fast.valid31:                             ; preds = %managed.fast.check29
  br label %managed.end33

managed.slow32:                                   ; preds = %managed.fast.check29, %managed.fast.load30, %error.propagate26
  %managed.slow.ptr46 = call ptr @absolute_managed_get(i64 %cleanup.handle28)
  br label %managed.end33

managed.end33:                                    ; preds = %managed.slow32, %managed.fast.valid31
  %managed.final.ptr47 = phi ptr [ %managed.ptr44, %managed.fast.valid31 ], [ %managed.slow.ptr46, %managed.slow32 ]
  %aggregate.present50 = icmp ne ptr %managed.final.ptr47, null
  br i1 %aggregate.present50, label %aggregate.cleanup48, label %aggregate.cleanup.end49

aggregate.cleanup48:                              ; preds = %managed.end33
  %aggregate.vtable51 = load ptr, ptr %managed.final.ptr47, align 8
  %aggregate.destroy.slot52 = getelementptr ptr, ptr %aggregate.vtable51, i64 0
  %aggregate.destroy53 = load ptr, ptr %aggregate.destroy.slot52, align 8
  call void %aggregate.destroy53(ptr %managed.final.ptr47)
  br label %aggregate.cleanup.end49

aggregate.cleanup.end49:                          ; preds = %aggregate.cleanup48, %managed.end33
  call void @absolute_managed_destroy(i64 %cleanup.handle28)
  store i64 0, ptr %map, align 4
  store ptr null, ptr %map.cached.pointee, align 8
  call void @absolute_error_report()
  ret i32 1

while.condition55:                                ; preds = %if.end, %while.end
  %i.value58 = load i32, ptr %i, align 4
  %N.value59 = load i32, ptr %N, align 4
  %less60 = icmp slt i32 %i.value58, %N.value59
  br i1 %less60, label %while.body56, label %while.end57

while.body56:                                     ; preds = %while.condition55
  %i.value62 = load i32, ptr %i, align 4
  %mul63 = mul i32 %i.value62, 17
  %add64 = add i32 %mul63, 31
  store i32 %add64, ptr %k61, align 4
  %map.value65 = load i64, ptr %map, align 4
  %map.cached.pointee66 = load ptr, ptr %map.cached.pointee, align 8
  %k.value67 = load i32, ptr %k61, align 4
  %containsKey.result = call i1 @"std.collections.HashMap<int32,int32>.containsKey$int32"(ptr %map.cached.pointee66, i32 %k.value67)
  %error.pending68 = call i1 @absolute_error_pending()
  br i1 %error.pending68, label %error.propagate69, label %error.continue70

while.end57:                                      ; preds = %while.condition55
  %checksum.value = load i64, ptr %checksum, align 4
  %print.result = call i32 (ptr, ...) @printf(ptr @print.format, i64 %checksum.value)
  %cleanup.handle133 = load i64, ptr %map, align 4
  %managed.id139 = trunc i64 %cleanup.handle133 to i32
  %3 = lshr i64 %cleanup.handle133, 32
  %managed.gen140 = trunc i64 %3 to i32
  %managed.slots.size141 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds142 = icmp ult i32 %managed.id139, %managed.slots.size141
  br i1 %managed.in.bounds142, label %managed.fast.load135, label %managed.slow137

if.end:                                           ; preds = %error.continue102, %error.continue70
  %assignment.current131 = load i32, ptr %i, align 4
  %add132 = add i32 %assignment.current131, 1
  store i32 %add132, ptr %i, align 4
  br label %while.condition55

error.propagate69:                                ; preds = %while.body56
  %cleanup.handle71 = load i64, ptr %map, align 4
  %managed.id77 = trunc i64 %cleanup.handle71 to i32
  %4 = lshr i64 %cleanup.handle71, 32
  %managed.gen78 = trunc i64 %4 to i32
  %managed.slots.size79 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds80 = icmp ult i32 %managed.id77, %managed.slots.size79
  br i1 %managed.in.bounds80, label %managed.fast.load73, label %managed.slow75

error.continue70:                                 ; preds = %while.body56
  br i1 %containsKey.result, label %if.body, label %if.end

managed.fast.check72:                             ; preds = %managed.fast.load73
  %managed.slot.ptr.field86 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr82, i32 0, i32 0
  %managed.ptr87 = load ptr, ptr %managed.slot.ptr.field86, align 8
  %managed.ptr.not.null88 = icmp ne ptr %managed.ptr87, null
  br i1 %managed.ptr.not.null88, label %managed.fast.valid74, label %managed.slow75

managed.fast.load73:                              ; preds = %error.propagate69
  %managed.slots.data81 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr82 = getelementptr %absolute.Slot, ptr %managed.slots.data81, i32 %managed.id77
  %managed.slot.gen.ptr83 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr82, i32 0, i32 1
  %managed.slot.gen84 = load i32, ptr %managed.slot.gen.ptr83, align 4
  %managed.gen.match85 = icmp eq i32 %managed.slot.gen84, %managed.gen78
  br i1 %managed.gen.match85, label %managed.fast.check72, label %managed.slow75

managed.fast.valid74:                             ; preds = %managed.fast.check72
  br label %managed.end76

managed.slow75:                                   ; preds = %managed.fast.check72, %managed.fast.load73, %error.propagate69
  %managed.slow.ptr89 = call ptr @absolute_managed_get(i64 %cleanup.handle71)
  br label %managed.end76

managed.end76:                                    ; preds = %managed.slow75, %managed.fast.valid74
  %managed.final.ptr90 = phi ptr [ %managed.ptr87, %managed.fast.valid74 ], [ %managed.slow.ptr89, %managed.slow75 ]
  %aggregate.present93 = icmp ne ptr %managed.final.ptr90, null
  br i1 %aggregate.present93, label %aggregate.cleanup91, label %aggregate.cleanup.end92

aggregate.cleanup91:                              ; preds = %managed.end76
  %aggregate.vtable94 = load ptr, ptr %managed.final.ptr90, align 8
  %aggregate.destroy.slot95 = getelementptr ptr, ptr %aggregate.vtable94, i64 0
  %aggregate.destroy96 = load ptr, ptr %aggregate.destroy.slot95, align 8
  call void %aggregate.destroy96(ptr %managed.final.ptr90)
  br label %aggregate.cleanup.end92

aggregate.cleanup.end92:                          ; preds = %aggregate.cleanup91, %managed.end76
  call void @absolute_managed_destroy(i64 %cleanup.handle71)
  store i64 0, ptr %map, align 4
  store ptr null, ptr %map.cached.pointee, align 8
  call void @absolute_error_report()
  ret i32 1

if.body:                                          ; preds = %error.continue70
  %k.value97 = load i32, ptr %k61, align 4
  %map.value98 = load i64, ptr %map, align 4
  %map.cached.pointee99 = load ptr, ptr %map.cached.pointee, align 8
  %property.result = call i32 @"std.collections.HashMap<int32,int32>.__absolute_indexer_get$int32"(ptr %map.cached.pointee99, i32 %k.value97)
  %error.pending100 = call i1 @absolute_error_pending()
  br i1 %error.pending100, label %error.propagate101, label %error.continue102

error.propagate101:                               ; preds = %if.body
  %cleanup.handle103 = load i64, ptr %map, align 4
  %managed.id109 = trunc i64 %cleanup.handle103 to i32
  %5 = lshr i64 %cleanup.handle103, 32
  %managed.gen110 = trunc i64 %5 to i32
  %managed.slots.size111 = load i32, ptr @absolute_managed_slots_size, align 4
  %managed.in.bounds112 = icmp ult i32 %managed.id109, %managed.slots.size111
  br i1 %managed.in.bounds112, label %managed.fast.load105, label %managed.slow107

error.continue102:                                ; preds = %if.body
  %assignment.current129 = load i64, ptr %checksum, align 4
  %int.cast = sext i32 %property.result to i64
  %add130 = add i64 %assignment.current129, %int.cast
  store i64 %add130, ptr %checksum, align 4
  br label %if.end

managed.fast.check104:                            ; preds = %managed.fast.load105
  %managed.slot.ptr.field118 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr114, i32 0, i32 0
  %managed.ptr119 = load ptr, ptr %managed.slot.ptr.field118, align 8
  %managed.ptr.not.null120 = icmp ne ptr %managed.ptr119, null
  br i1 %managed.ptr.not.null120, label %managed.fast.valid106, label %managed.slow107

managed.fast.load105:                             ; preds = %error.propagate101
  %managed.slots.data113 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr114 = getelementptr %absolute.Slot, ptr %managed.slots.data113, i32 %managed.id109
  %managed.slot.gen.ptr115 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr114, i32 0, i32 1
  %managed.slot.gen116 = load i32, ptr %managed.slot.gen.ptr115, align 4
  %managed.gen.match117 = icmp eq i32 %managed.slot.gen116, %managed.gen110
  br i1 %managed.gen.match117, label %managed.fast.check104, label %managed.slow107

managed.fast.valid106:                            ; preds = %managed.fast.check104
  br label %managed.end108

managed.slow107:                                  ; preds = %managed.fast.check104, %managed.fast.load105, %error.propagate101
  %managed.slow.ptr121 = call ptr @absolute_managed_get(i64 %cleanup.handle103)
  br label %managed.end108

managed.end108:                                   ; preds = %managed.slow107, %managed.fast.valid106
  %managed.final.ptr122 = phi ptr [ %managed.ptr119, %managed.fast.valid106 ], [ %managed.slow.ptr121, %managed.slow107 ]
  %aggregate.present125 = icmp ne ptr %managed.final.ptr122, null
  br i1 %aggregate.present125, label %aggregate.cleanup123, label %aggregate.cleanup.end124

aggregate.cleanup123:                             ; preds = %managed.end108
  %aggregate.vtable126 = load ptr, ptr %managed.final.ptr122, align 8
  %aggregate.destroy.slot127 = getelementptr ptr, ptr %aggregate.vtable126, i64 0
  %aggregate.destroy128 = load ptr, ptr %aggregate.destroy.slot127, align 8
  call void %aggregate.destroy128(ptr %managed.final.ptr122)
  br label %aggregate.cleanup.end124

aggregate.cleanup.end124:                         ; preds = %aggregate.cleanup123, %managed.end108
  call void @absolute_managed_destroy(i64 %cleanup.handle103)
  store i64 0, ptr %map, align 4
  store ptr null, ptr %map.cached.pointee, align 8
  call void @absolute_error_report()
  ret i32 1

managed.fast.check134:                            ; preds = %managed.fast.load135
  %managed.slot.ptr.field148 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr144, i32 0, i32 0
  %managed.ptr149 = load ptr, ptr %managed.slot.ptr.field148, align 8
  %managed.ptr.not.null150 = icmp ne ptr %managed.ptr149, null
  br i1 %managed.ptr.not.null150, label %managed.fast.valid136, label %managed.slow137

managed.fast.load135:                             ; preds = %while.end57
  %managed.slots.data143 = load ptr, ptr @absolute_managed_slots_data, align 8
  %managed.slot.ptr144 = getelementptr %absolute.Slot, ptr %managed.slots.data143, i32 %managed.id139
  %managed.slot.gen.ptr145 = getelementptr inbounds %absolute.Slot, ptr %managed.slot.ptr144, i32 0, i32 1
  %managed.slot.gen146 = load i32, ptr %managed.slot.gen.ptr145, align 4
  %managed.gen.match147 = icmp eq i32 %managed.slot.gen146, %managed.gen140
  br i1 %managed.gen.match147, label %managed.fast.check134, label %managed.slow137

managed.fast.valid136:                            ; preds = %managed.fast.check134
  br label %managed.end138

managed.slow137:                                  ; preds = %managed.fast.check134, %managed.fast.load135, %while.end57
  %managed.slow.ptr151 = call ptr @absolute_managed_get(i64 %cleanup.handle133)
  br label %managed.end138

managed.end138:                                   ; preds = %managed.slow137, %managed.fast.valid136
  %managed.final.ptr152 = phi ptr [ %managed.ptr149, %managed.fast.valid136 ], [ %managed.slow.ptr151, %managed.slow137 ]
  %aggregate.present155 = icmp ne ptr %managed.final.ptr152, null
  br i1 %aggregate.present155, label %aggregate.cleanup153, label %aggregate.cleanup.end154

aggregate.cleanup153:                             ; preds = %managed.end138
  %aggregate.vtable156 = load ptr, ptr %managed.final.ptr152, align 8
  %aggregate.destroy.slot157 = getelementptr ptr, ptr %aggregate.vtable156, i64 0
  %aggregate.destroy158 = load ptr, ptr %aggregate.destroy.slot157, align 8
  call void %aggregate.destroy158(ptr %managed.final.ptr152)
  br label %aggregate.cleanup.end154

aggregate.cleanup.end154:                         ; preds = %aggregate.cleanup153, %managed.end138
  call void @absolute_managed_destroy(i64 %cleanup.handle133)
  store i64 0, ptr %map, align 4
  store ptr null, ptr %map.cached.pointee, align 8
  ret i32 0
}

declare i64 @absolute_managed_create(i64)

declare void @absolute_managed_set_type(i64, i64)

declare ptr @absolute_managed_require(i64)

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #0

define internal i64 @__absolute.closure.invoke.hashInt32(ptr %0, i32 %1) {
entry:
  %2 = call i64 @hashInt32(i32 %1)
  ret i64 %2
}

define internal i1 @__absolute.closure.invoke.equalsInt32(ptr %0, i32 %1, i32 %2) {
entry:
  %3 = call i1 @equalsInt32(i32 %1, i32 %2)
  ret i1 %3
}

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

define internal void @__absolute.closure.retain(ptr %0) {
entry:
  %1 = icmp ne ptr %0, null
  br i1 %1, label %update, label %complete

update:                                           ; preds = %entry
  %closure.count.address = getelementptr inbounds %absolute.closure, ptr %0, i32 0, i32 0
  %closure.count = load i64, ptr %closure.count.address, align 4
  %2 = icmp sge i64 %closure.count, 0
  br i1 %2, label %increment, label %complete

complete:                                         ; preds = %increment, %update, %entry
  ret void

increment:                                        ; preds = %update
  %3 = add i64 %closure.count, 1
  store i64 %3, ptr %closure.count.address, align 4
  br label %complete
}

define internal void @__absolute.closure.release(ptr %0) {
entry:
  %1 = icmp ne ptr %0, null
  br i1 %1, label %update, label %complete

update:                                           ; preds = %entry
  %closure.count.address = getelementptr inbounds %absolute.closure, ptr %0, i32 0, i32 0
  %closure.count = load i64, ptr %closure.count.address, align 4
  %2 = icmp sge i64 %closure.count, 0
  br i1 %2, label %decrement, label %complete

decrement:                                        ; preds = %update
  %closure.count.next = sub i64 %closure.count, 1
  store i64 %closure.count.next, ptr %closure.count.address, align 4
  %3 = icmp eq i64 %closure.count.next, 0
  br i1 %3, label %destroy, label %complete

destroy:                                          ; preds = %decrement
  %closure.destroy.address = getelementptr inbounds %absolute.closure, ptr %0, i32 0, i32 3
  %closure.destroy = load ptr, ptr %closure.destroy.address, align 8
  %4 = icmp ne ptr %closure.destroy, null
  br i1 %4, label %destroy.env, label %free

destroy.env:                                      ; preds = %destroy
  %closure.environment.address = getelementptr inbounds %absolute.closure, ptr %0, i32 0, i32 2
  %closure.environment = load ptr, ptr %closure.environment.address, align 8
  call void %closure.destroy(ptr %closure.environment)
  br label %free

free:                                             ; preds = %destroy.env, %destroy
  call void @free(ptr %0)
  br label %complete

complete:                                         ; preds = %free, %decrement, %update, %entry
  ret void
}

declare i64 @absolute_managed_type(i64)

declare void @absolute_error_set(i64, i64)

attributes #0 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #1 = { cold noreturn }
attributes #2 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }

!0 = !{!"branch_weights", i32 2000, i32 1}
