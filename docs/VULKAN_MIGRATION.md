# План Vulkan для Huynity

У Huynity уже есть семантическая граница между подготовкой кадра и бэкендом. `RenderSnapshot` описывает упорядоченные World3D, World2D и HUD, материалы, текстуры, камеры и источники света. На этой границе остаётся совместимость OpenGL и SoftwareRenderer. [Render Contracts](../engine/render_contracts/include/huynity/render_contracts/render_snapshot.h)

## Границы и владение

| Область | Решение |
|---|---|
| Core | Владеет сценой, извлекает геометрию, параметры проходов и видимость. Определяет порядок прозрачности и даёт стабильную идентичность ресурса с ревизией содержимого. |
| Бэкенд | Записывает команды из снимка в нативном представлении. OpenGL использует GL-контекст и локальные кеши. Vulkan владеет device, очередями, pipeline, дескрипторами, памятью и версиями GPU-ресурсов. |
| Поверхность | Адаптер окна владеет SDL-вводом и нативным окном. Инициализация OpenGL получает context и swap. Vulkan получает расширения WSI и поверхность, а swapchain остаётся в Vulkan-бэкенде. Выбор бэкенда, размер, resize, создание кадра, present и capture образуют небольшой общий контракт приложения. |
| Захват | Разовый путь GL readback и Vulkan image-to-staging. Учитывать формат, ориентацию, stride и преобразование цвета. Данные GPU остаются валидными до завершения копирования. |

Устранить адресные IDs и нетипизированное чтение индексов до асинхронного Vulkan. Текущая прямая ветка GL в `Engine::step` и GL-методы `WindowAdapter` показывают места разделения. CPU `ImageBuffer` представляет headless-результат и capture. Обычный present аппаратного бэкенда не требует CPU-кадра. [Engine](../engine/core/src/engine.cpp) · [WindowAdapter](../adapters/window/include/huynity/adapters/window/window_adapter.h)

## Ресурсы кадра и синхронизация

1. Начать с одной graphics queue, primary command buffer на слот и двух слотов в полёте после отдельной проверки первого однокадрового варианта. Каждый слот владеет command pool, дескрипторами кадра, диапазоном staging и очередью отложенного освобождения.
2. Кадровый fence или timeline value разрешает повторное выделение ресурсов слота. Команды удерживают GPU-версии до завершения последнего использующего их submit. Изменение геометрии публикует новую версию, а прежнюю ставит на отложенное удаление.
3. Acquire и present используют binary semaphores. Семафор, который ждёт present, привязан к swapchain image. Отдельно обработать `OUT_OF_DATE`, `SUBOPTIMAL`, минимизированное окно и пересоздание swapchain. [Khronos о переиспользовании семафоров](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
4. Использовать `synchronization2` для upload → vertex/input или shader read, shadow attachment → sampled image и color attachment → present. Стадии и access masks следуют фактической зависимости. [Khronos о синхронизации](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
5. Выделять GPU-память крупными блоками с подвыделениями. Стартовый staging работает по диапазонам слота кадра, загрузки группируются. Отдельную transfer queue и конкурентную запись включать по результатам измерений. Для параллельной записи каждый worker получает свой command pool в слоте. Сохранять порядок зависимых и прозрачных команд. [Khronos о потоках](https://docs.vulkan.org/guide/latest/threading.html)

## Pipeline и шейдеры

- Компилировать Vulkan GLSL в SPIR-V при сборке. Зафиксировать layout для per-frame, материала и объекта. Малые per-object данные начать с push constants или смещений в буфере. Материалы и текстуры использовать через переиспользуемые наборы дескрипторов.
- Ключ pipeline включает версии шейдеров, режим геометрии, состояния глубины, кулинга и blending, форматы color/depth. Сохраняемый cache проверяет совместимость device/driver. Создание pipeline и дескрипторных пулов измеряется отдельно от тёплого кадра.
- Выделить минимальные зависимости между shadow, world и HUD. Начать с фиксированной последовательности. Потребность в универсальном render graph можно оценить по реальным новым passes.
- Сверить winding, ориентацию framebuffer, диапазон clip-space depth, линейный цвет и sRGB, параметры shadow bias, fog, sky и смешивание. OpenGL и Vulkan сравнивать по областям контрольного кадра с заданными допусками.

## Этапы и условия перехода

1. **Подготовка.** Снять [воспроизводимый baseline](PERFORMANCE_BASELINE.md) и закрепить контракт ресурсов, resize, времени жизни окна и GPU. Сохранить тесты OpenGL на тени, 2D/HUD и capture.
2. **Опциональный backend.** Собрать Vulkan только при наличии заголовков и loader. На Linux проверить device capabilities, Mesa ICD и `VK_LAYER_KHRONOS_validation`. Стартовый сценарий — clear, resize, представление и корректное освобождение.
3. **Вертикальный срез.** Передать `RenderSnapshot` одного текстурированного объекта, добавить стабильные mesh/texture IDs, staging upload и readback. Добиться чистой validation, включая synchronization validation.
4. **Паритет.** Перенести свет, shadow pass, sky, затем World2D/HUD, прозрачность и capture. Запустить одинаковые сценарии на обоих бэкендах. Проверить resize, минимизацию, повторное создание поверхностей и обновление ресурса при незавершённом кадре.
5. **Масштабирование.** Сравнить CPU submit, GPU timestamps, медиану, P95/P99, число draws, объём uploads и занятую память на одинаковых настройках. Измерения VSync и frame-in-flight оценивать отдельно. Сохранять OpenGL рабочим и доступным для выбора.

В текущем исходном дереве Vulkan-бэкенда нет. В окружении найдены Mesa ICD и Vulkan loader. Заголовки SDK, утилиты и validation layers потребуются для этапа опционального бэкенда.
