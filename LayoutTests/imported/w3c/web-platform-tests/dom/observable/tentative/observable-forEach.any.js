// promise_test(async (t) => {
//   const source = new Observable((subscriber) => {
//     subscriber.next(1);
//     subscriber.next(2);
//     subscriber.next(3);
//     subscriber.complete();
//   });

//   const results = [];

//   const completion = source.forEach((value) => {
//     results.push(value);
//   });

//   assert_array_equals(results, [1, 2, 3]);
//   await completion;
// }, "forEach(): Visitor callback called synchronously for each value");

// promise_test(async () => {
//   let error = new Error('error');

//   const observable = new Observable(subscriber => {});
//   const controller = new AbortController();

//   const completion = observable.forEach(() => { }, { signal: controller.signal });

//   controller.abort(error);

//   try {
// 	  await completion;
// 	  assert_unreached("Operator promise must not resolve if its abort signal " +
//       "is rejected");
//   } catch(e) {
// 	  assert_equals(e, error);
//   }
// }, "forEach(): Aborting the passed-in signal rejects the returned promise");

// promise_test(async () => {
//   // Promise-returning operator with an aborted signal must *immediately* reject
//   // the returned Promise, which means code "awaiting" it should run before any
//   // subsequent Promise resolution/rejection handlers are run.
//   let postSubscriptionPromiseResolved = false;

//   const observable = new Observable(subscriber => {
//     const inactive = !subscriber.active;
//     subscriptionImmediatelyInactive = inactive;
//   });

//   const rejectedPromise = observable.forEach(() => { }, {signal: AbortSignal.abort()})
//   .then(() => {
//     assert_unreached("Operator promise must not resolve its abort signal is " +
//                      "rejected");
//   }, () => {
//     // See the documentation above. The rejection handler (i.e., this code) for
//     // immediately-aborted operator Promises runs before any later-scheduled
//     // Promise resolution/rejections.
//     assert_false(postSubscriptionPromiseResolved,
//         "Operator promise rejects before later promise");
//   });
//   const postSubscriptionPromise =
//       Promise.resolve().then(() => postSubscriptionPromiseResolved = true);

//   await rejectedPromise;
// }, "forEach(): Subscribing with an aborted signal returns an immediately " +
//    "rejected promise");

// promise_test(async (t) => {
//   const error = new Error("error");
//   const source = new Observable((subscriber) => {
//     throw error;
//   });

//   try {
//     await source.forEach(() => {
//       assert_unreached("Visitor callback is not invoked when Observable errors");
//     });
//     assert_unreached("forEach() promise does not resolve when Observable errors");
//   } catch (e) {
//     assert_equals(e, error);
//   }
// }, "forEach(): Errors thrown by Observable reject the returned promise");

// promise_test(async (t) => {
//   const error = new Error("error");
//   const source = new Observable((subscriber) => {
//     subscriber.error(error);
//   });

//   try {
//     await source.forEach(() => {
//       assert_unreached("Visitor callback is not invoked when Observable errors");
//     });
//     assert_unreached("forEach() promise does not resolve when Observable errors");
//   } catch (reason) {
//     assert_equals(reason, error);
//   }
// }, "forEach(): Errors pushed by Observable reject the returned promise");

// promise_test(async (t) => {
//   // This will be assigned when `source`'s teardown is called during
//   // unsubscription.
//   let abortReason = null;

//   const error = new Error("error");
//   const source = new Observable((subscriber) => {
//     // Should be called from within the second `next()` call below, when the
//     // `forEach()` visitor callback throws an error, because that triggers
//     // unsubscription from `source`.
//     subscriber.addTeardown(() => abortReason = subscriber.signal.reason);

//     subscriber.next(1);
//     subscriber.next(2);
//     subscriber.next(3);
//     subscriber.complete();
//   });

//   const results = [];

//   const completion = source.forEach((value) => {
//     results.push(value);
//     if (value === 2) {
//       throw error;
//     }
//   });

//   assert_array_equals(results, [1, 2]);
//   assert_equals(abortReason, error,
//       "forEach() visitor callback throwing an error triggers unsubscription " +
//       "from the source observable, with the correct abort reason");

//   try {
//     await completion;
//     assert_unreached("forEach() promise does not resolve when visitor throws");
//   } catch (e) {
//     assert_equals(e, error);
//   }
// }, "forEach(): Errors thrown in the visitor callback reject the promise and " +
//    "unsubscribe from the source");

// See https://github.com/WICG/observable/issues/96 for discussion about the
// timing of Observable AbortSignal `abort` firing and promise rejection.
promise_test(async t => {
  const error = new Error('custom error');
  let rejectionError = null;
  let outerAbortEventMicrotaskRun = false,
      forEachPromiseRejectionMicrotaskRun = false,
      innerAbortEventMicrotaskRun = false;

  const source = new Observable(subscriber => {
    subscriber.signal.addEventListener('abort', () => {
    console.log('source::abort')
      queueMicrotask(() => {
      console.log('source::abort:microtask')
        // assert_true(outerAbortEventMicrotaskRun,
        //     "Inner abort: outer abort microtask has fired");
        // assert_true(forEachPromiseRejectionMicrotaskRun,
        //     "Inner abort: forEach rejection microtask has fired");
        // assert_false(innerAbortEventMicrotaskRun,
        //     "Inner abort: inner abort microtask has not fired");

        innerAbortEventMicrotaskRun = true;
      });
    });
  });

  const controller = new AbortController();
  controller.signal.addEventListener('abort', () => {
  console.log("controller::abort")
    queueMicrotask(() => {
    console.log("controller::abort:microtask")
      // assert_false(outerAbortEventMicrotaskRun,
      //     "Outer abort: outer abort microtask has not fired");
      // assert_false(forEachPromiseRejectionMicrotaskRun,
      //     "Outer abort: forEach rejection microtask has not fired");
      // assert_false(innerAbortEventMicrotaskRun,
      //     "Outer abort: inner abort microtask has not fired");

      outerAbortEventMicrotaskRun = true;
    });
  });

  const promise = source.forEach(() => {}, {signal: controller.signal}).catch(e => {
  console.log('rejected')
    rejectionError = e;
    // assert_true(outerAbortEventMicrotaskRun,
    //     "Promise rejection: outer abort microtask has fired");
    // assert_false(forEachPromiseRejectionMicrotaskRun,
    //     "Promise rejection: forEach rejection microtask has not fired");
    // assert_false(innerAbortEventMicrotaskRun,
    //     "Promise rejection: inner abort microtask has not fired");

    forEachPromiseRejectionMicrotaskRun = true;
  });

  // This should trigger the following, in this order:
  //   1. Fire the `abort` event at the outer AbortSignal, whose handler
  //      manually queues a microtask.
  //   2. Calls "signal abort" on the outer signal's dependent signals. This
  //      queues a microtask to reject the `forEach()` promise.
  //   3. Fire the `abort` event at the inner AbortSignal, whose handler
  //      manually queues a microtask.
console.log('before abort');
  controller.abort(error);
  console.log('after abort');

  // After a single task, assert that everything has happened correctly (and
  // incrementally in the right order);
  console.log('before promise');
  await new Promise(resolve => {
    t.step_timeout(resolve);
  });
  console.log('after promise');
  // assert_true(outerAbortEventMicrotaskRun,
  //     "Final: outer abort microtask has fired");
  // assert_true(forEachPromiseRejectionMicrotaskRun,
  //     "Final: forEach rejection microtask has fired");
  // assert_true(innerAbortEventMicrotaskRun,
  //     "Final: inner abort microtask has fired");
  assert_equals(rejectionError, error, "Promise is rejected with the right " +
      "value");
}, "forEach(): Visitor callback rejection microtask ordering");

// promise_test(async (t) => {
//   const source = new Observable((subscriber) => {
//     subscriber.next(1);
//     subscriber.next(2);
//     subscriber.next(3);
//     subscriber.complete();
//   });

//   const results = [];

//   const completion = source.forEach((value) => {
//     results.push(value);
//   });

//   assert_array_equals(results, [1, 2, 3]);

//   const completionValue = await completion;
//   assert_equals(completionValue, undefined, "Promise resolves with undefined");
// }, "forEach(): Promise resolves with undefined");
