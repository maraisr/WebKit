promise_test(async (t) => {
  const source = new Observable((subscriber) => {
    subscriber.next(1);
    subscriber.next(2);
    subscriber.next(3);
    subscriber.complete();
  });

  const results = [];

  const completion = source.forEach((value) => {
    results.push(value);
  });

  assert_array_equals(results, [1, 2, 3]);
  await completion;
}, "forEach(): Visitor callback called synchronously for each value");

promise_test(async (t) => {
  const error = new Error("error");
  const source = new Observable((subscriber) => {
    throw error;
  });

  try {
    await source.forEach(() => {
      assert_unreached("Visitor callback is not invoked when Observable errors");
    });
    assert_unreached("forEach() promise does not resolve when Observable errors");
  } catch (e) {
    assert_equals(e, error);
  }
}, "forEach(): Errors thrown by Observable reject the returned promise");

promise_test(async (t) => {
  const error = new Error("error");
  const source = new Observable((subscriber) => {
    subscriber.error(error);
  });

  try {
    await source.forEach(() => {
      assert_unreached("Visitor callback is not invoked when Observable errors");
    });
    assert_unreached("forEach() promise does not resolve when Observable errors");
  } catch (reason) {
    assert_equals(reason, error);
  }
}, "forEach(): Errors pushed by Observable reject the returned promise");

promise_test(async (t) => {
  // This will be assigned when `source`'s teardown is called during
  // unsubscription.
  let abortReason = null;

  const error = new Error("error");
  const source = new Observable((subscriber) => {
    // Should be called from within the second `next()` call below, when the
    // `forEach()` visitor callback throws an error, because that triggers
    // unsubscription from `source`.
    subscriber.addTeardown(() => abortReason = subscriber.signal.reason);

    subscriber.next(1);
    subscriber.next(2);
    subscriber.next(3);
    subscriber.complete();
  });

  const results = [];

  const completion = source.forEach((value) => {
    results.push(value);
    if (value === 2) {
      throw error;
    }
  });

  assert_array_equals(results, [1, 2]);
  assert_equals(abortReason, error,
      "forEach() visitor callback throwing an error triggers unsubscription " +
      "from the source observable, with the correct abort reason");

  try {
    await completion;
    assert_unreached("forEach() promise does not resolve when visitor throws");
  } catch (e) {
    assert_equals(e, error);
  }
}, "forEach(): Errors thrown in the visitor callback reject the promise and " +
   "unsubscribe from the source");

promise_test(async () => {
  let error = new Error('error');

  const observable = new Observable(subscriber => {});
  const controller = new AbortController();

  const completion = observable.forEach(() => { }, { signal: controller.signal });

  controller.abort(error);

  try {
	  await completion;
	  assert_unreached("Operator promise must not resolve if its abort signal " +
      "is rejected");
  } catch(e) {
	  assert_equals(e, error);
  }
}, "forEach(): Aborting the passed-in signal rejects the returned promise");

promise_test(async () => {
  // Promise-returning operator with an aborted signal must *immediately* reject
  // the returned Promise, which means code "awaiting" it should run before any
  // subsequent Promise resolution/rejection handlers are run.
  let postSubscriptionPromiseResolved = false;

  const observable = new Observable(subscriber => {
    const inactive = !subscriber.active;
    subscriptionImmediatelyInactive = inactive;
  });

  const rejectedPromise = observable.forEach(() => { }, {signal: AbortSignal.abort()})
  .then(() => {
    assert_unreached("Operator promise must not resolve its abort signal is " +
                     "rejected");
  }, () => {
    // See the documentation above. The rejection handler (i.e., this code) for
    // immediately-aborted operator Promises runs before any later-scheduled
    // Promise resolution/rejections.
    assert_false(postSubscriptionPromiseResolved,
        "Operator promise rejects before later promise");
  });
  const postSubscriptionPromise =
      Promise.resolve().then(() => postSubscriptionPromiseResolved = true);

  await rejectedPromise;
}, "forEach(): Subscribing with an aborted signal returns an immediately " +
   "rejected promise");

// See https://github.com/WICG/observable/issues/96 for discussion about the
// timing of Observable AbortSignal `abort` firing and promise rejection.
promise_test(async () => {
  const error = new Error('error');
  const results = [];

  const observable = new Observable(subscriber => {
    results.push(`Subscribed. active: ${subscriber.active}`);

    subscriber.signal.addEventListener('abort', e => {
      results.push("Inner signal abort event");
      Promise.resolve("Inner signal Promise").then(value => results.push(value));
    });

    subscriber.addTeardown(() => {
      results.push("Teardown");
      Promise.resolve("Teardown Promise").then(value => results.push(value));
    });
  });

  const controller = new AbortController();
  controller.signal.addEventListener('abort', e => {
    results.push("Outer signal abort event");
    Promise.resolve("Outer signal Promise").then(value => results.push(value));
  });

  // Subscribe.
  observable.forEach(() => { }, { signal: controller.signal })
    .catch(e => {
      assert_equals(e, error);
      results.push("Operator promise rejected");
  });
  controller.abort(error);

  assert_array_equals(results, [
    "Subscribed. active: true",
    "Inner signal abort event",
    "Teardown",
    "Outer signal abort event",
  ], "Events and teardowns are fired in the right ordered");

  // Everything microtask above should be queued up by now, so queue one more
  // final microtask that will run after all of the others, wait for it, and the
  // check `results` is right.
  await Promise.resolve();
  assert_array_equals(results, [
    "Subscribed. active: true",
    "Inner signal abort event",
    "Teardown",
    "Outer signal abort event",
    "Operator promise rejected",
    "Inner signal Promise",
    "Teardown Promise",
    "Outer signal Promise",
  ], "Promises resolve in the right order");
}, "forEach(): Operator Promise abort ordering");

promise_test(async (t) => {
  const source = new Observable((subscriber) => {
    subscriber.next(1);
    subscriber.next(2);
    subscriber.next(3);
    subscriber.complete();
  });

  const results = [];

  const completion = source.forEach((value) => {
    results.push(value);
  });

  assert_array_equals(results, [1, 2, 3]);

  const completionValue = await completion;
  assert_equals(completionValue, undefined, "Promise resolves with undefined");
}, "forEach(): Promise resolves with undefined");
